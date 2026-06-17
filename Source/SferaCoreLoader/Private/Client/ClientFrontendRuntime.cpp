#include "Client/ClientFrontendRuntime.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <chrono>
#include <thread>
#include <memory>

namespace Sfera {
FClientFrontendRuntime::FClientFrontendRuntime(FLogger* logger) : Log(logger), Session(logger) {}
FClientFrontendRuntime::~FClientFrontendRuntime() { Shutdown(); }

FStatus FClientFrontendRuntime::CreateShell() {
    if (ShellCreated) { return FStatus::Ok(); }
    FWindowDesc windowDesc;
    windowDesc.Width = 1024;
    windowDesc.Height = 768;
    FStatus status = Window.Create(windowDesc, Log);
    if (!status.IsOk()) { return status; }
    {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        LoadingModel.SetStage("starting frontend", 0.02f);
        LoadingModel.AddStatusLine("window: created");
    }
    Window.SetPaintCallback([this](HDC__* dc, const tagRECT& rect) {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        if (D3DInitialized.load()) { return; }
        FLoadingScreenPainter painter(LoadingModel);
        painter.Paint(dc, rect);
    });
    Window.Show();
    Window.RequestRepaint();
    ShellCreated = true;
    if (Log) { Log->Info("frontend shell created before data bootstrap"); }
    return FStatus::Ok();
}

void FClientFrontendRuntime::SetStage(std::string stage, float progress) {
    {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        LoadingModel.SetStage(std::move(stage), progress);
        if (Log) { Log->Info("frontend stage: " + LoadingModel.Stage() + ", progress=" + std::to_string(LoadingModel.Progress())); }
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

void FClientFrontendRuntime::AddStatusLine(std::string line) {
    {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        LoadingModel.AddStatusLine(std::move(line));
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

bool FClientFrontendRuntime::PumpUi() {
    if (!ShellCreated) { return true; }
    bool open = Window.PumpMessages();
    return open && Window.IsOpen();
}

void FClientFrontendRuntime::InitializeLoadingResources(const FResourceManager& resources) {
    if (!ShellCreated) { return; }
    {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        UiResources.BuildManifest(resources, Log);
        LoadingModel.Initialize(resources, Log);
        const auto& uiManifest = UiResources.Manifest();
        LoadingModel.AddStatusLine("UI: docs=" + std::to_string(uiManifest.DocumentCount) + " parsed=" + std::to_string(uiManifest.ParsedCount) + " windows=" + std::to_string(uiManifest.WindowCount) + " controls=" + std::to_string(uiManifest.ControlCount));
        LoadingModel.SetStage("resources cataloged", 0.26f);
    }
    if (D3DInitialized.load()) { RepaintDirty = true; }
    else { Window.RequestRepaint(); }
}

void FClientFrontendRuntime::InitializeD3D9(const FResourceManager& resources) {
    if (!ShellCreated || D3DInitialized.load()) { return; }
    SetStage("initializing d3d9", 0.32f);
    FStatus d3dStatus;
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        d3dStatus = RenderDevice.Initialize(Window.Handle(), Window.Width(), Window.Height(), Log);
    }
    if (!d3dStatus.IsOk()) {
        if (Log) { Log->Warning("D3D9 frontend disabled: " + d3dStatus.Message()); }
        AddStatusLine("D3D9: fallback GDI loadscreen");
    } else {
        RenderResources = &resources;
        D3DInitialized.store(true);
        Window.SetPaintCallback([](HDC__*, const tagRECT&) {});
        AddStatusLine("D3D9: device ok");
        {
            std::lock_guard<std::recursive_mutex> modelLock(ModelMutex);
            std::lock_guard<std::mutex> renderLock(RenderMutex);
            LoadingModel.SetStage("preloading ui textures", 0.34f);
            RenderDevice.PreloadLoadingScreenTextures(resources, LoadingModel, Log);
        }
    }
    RenderDevice.InspectShaderResources(resources, Log);
    RepaintDirty = true;
    if (!D3DInitialized.load()) { Window.RequestRepaint(); }
}

void FClientFrontendRuntime::StartNetworkProbe(const FClientFrontendDesc& desc) {
    {
        std::lock_guard<std::mutex> lock(SessionMutex);
        Session.Configure(desc.Endpoint);
    }
    if (!ShellCreated) { return; }
    if (desc.TryNetworkProbe && desc.Endpoint) {
        SetStage("connecting to " + desc.Endpoint->Host + ":" + std::to_string(desc.Endpoint->Port), 0.72f);
        FStatus connectStatus;
        {
            std::lock_guard<std::mutex> lock(SessionMutex);
            connectStatus = Session.StartProbe(desc.NetworkConnectTimeoutMs);
        }
        if (!connectStatus.IsOk()) { AddStatusLine("network: " + connectStatus.Message()); }
        else { AddStatusLine("network: connected, protocol probe active"); }
    } else {
        AddStatusLine("network: probe skipped");
    }
    SetStage("waiting for server/menu transition", 0.78f);
}

void FClientFrontendRuntime::RenderFrame() {
    if (!ShellCreated || !Window.Handle()) { return; }
    if (!D3DInitialized.load() || !RenderResources) { return; }
    RECT client{};
    GetClientRect(Window.Handle(), &client);
    {
        std::lock_guard<std::recursive_mutex> modelLock(ModelMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        FStatus status = RenderDevice.RenderLoadingScreen(*RenderResources, LoadingModel, client, Log);
        if (!status.IsOk()) {
            if (Log) { Log->Warning("D3D9 render failed: " + status.Message()); }
            return;
        }
    }
}

FStatus FClientFrontendRuntime::RunEventLoop() {
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend event loop requested before window shell creation"); }
    if (Log) { Log->Info("frontend event loop started"); }
    LastPaint = std::chrono::steady_clock::now();
    auto lastRender = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    while (Window.IsOpen() && Window.PumpMessages()) {
        FInputSnapshot input = Window.ConsumeInputFrame();
        RECT client{};
        if (Window.Handle()) { GetClientRect(Window.Handle(), &client); }
        bool inputChanged = false;
        {
            std::lock_guard<std::recursive_mutex> lock(ModelMutex);
            inputChanged = LoadingModel.HandleInputFrame(input, client, Log);
            if (inputChanged) {
                const std::string& action = LoadingModel.Interaction().LastAction;
                if (!action.empty() && action != LastUiAction) {
                    LastUiAction = action;
                    LoadingModel.AddStatusLine("ui: " + action);
                }
            }
        }
        if (inputChanged) { RepaintDirty = true; }
        {
            std::lock_guard<std::mutex> lock(SessionMutex);
            Session.Tick();
        }
        UpdateStageFromSession();
        auto now = std::chrono::steady_clock::now();
        if (D3DInitialized.load()) {
            if (RepaintDirty || now - lastRender >= std::chrono::milliseconds(250)) {
                RenderFrame();
                lastRender = now;
                RepaintDirty = false;
            }
        } else if (RepaintDirty) {
            RequestRepaintThrottled();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (Log) { Log->Info("frontend event loop finished"); }
    return FStatus::Ok();
}

void FClientFrontendRuntime::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(SessionMutex);
        Session.Close();
    }
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.Shutdown();
    }
    Window.Destroy();
    ShellCreated = false;
    D3DInitialized.store(false);
    RenderResources = nullptr;
}

void FClientFrontendRuntime::RequestRepaintThrottled() {
    if (!ShellCreated) { return; }
    if (D3DInitialized.load()) { return; }
    auto now = std::chrono::steady_clock::now();
    if (RepaintDirty || now - LastPaint > std::chrono::milliseconds(50)) {
        Window.RequestRepaint();
        LastPaint = now;
    }
}

void FClientFrontendRuntime::UpdateStageFromSession() {
    EClientSessionStage stage;
    std::string stageText;
    {
        std::lock_guard<std::mutex> lock(SessionMutex);
        const auto& snapshot = Session.Snapshot();
        stage = snapshot.Stage;
        stageText = snapshot.StageText;
    }
    if (stage == LastSessionStage && stage != EClientSessionStage::ProbeReceiving) { return; }
    LastSessionStage = stage;
    {
        std::lock_guard<std::recursive_mutex> lock(ModelMutex);
        if (stage == EClientSessionStage::ProbeReceiving) { LoadingModel.SetStage(stageText, 0.86f); RepaintDirty = true; }
        else if (stage == EClientSessionStage::Connected) { LoadingModel.SetStage("connected; awaiting protocol", 0.80f); RepaintDirty = true; }
        else if (stage == EClientSessionStage::Failed) { LoadingModel.SetStage("network probe failed", 0.70f); RepaintDirty = true; }
    }
}

bool FClientFrontendRuntime::HasD3D() const { return D3DInitialized.load(); }
}
