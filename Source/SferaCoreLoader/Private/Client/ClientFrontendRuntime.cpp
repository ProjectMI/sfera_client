#include "Client/ClientFrontendRuntime.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <chrono>
#include <thread>

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
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetStage("starting frontend", 0.02f);
        Ui.AddStatusLine("window: created");
    }
    Window.SetPaintCallback([](HDC__*, const tagRECT&) {});
    Window.Show();
    ShellCreated = true;
    if (Log) { Log->Info("frontend shell created"); }
    return FStatus::Ok();
}

void FClientFrontendRuntime::SetStage(std::string stage, float progress) {
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetStage(std::move(stage), progress);
        if (Log) { Log->Info("frontend stage: " + Ui.Stage() + ", progress=" + std::to_string(Ui.Progress())); }
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

void FClientFrontendRuntime::AddStatusLine(std::string line) {
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.AddStatusLine(std::move(line));
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

bool FClientFrontendRuntime::PumpUi() {
    if (!ShellCreated) { return true; }
    bool open = Window.PumpMessages();
    return open && Window.IsOpen();
}

FStatus FClientFrontendRuntime::InitializeUiResources(const FResourceManager& resources) {
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }
    std::lock_guard<std::recursive_mutex> lock(UiMutex);
    FUiBootstrapDesc desc;
    FStatus status = Ui.Initialize(resources, desc, Log);
    if (!status.IsOk()) { return status; }
    Ui.SetStage("ui runtime initialized", 0.26f);
    RepaintDirty = true;
    return FStatus::Ok();
}

FStatus FClientFrontendRuntime::InitializeD3D9(const FResourceManager& resources) {
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }
    if (D3DInitialized.load()) { return FStatus::Ok(); }
    SetStage("initializing d3d9", 0.32f);
    FStatus d3dStatus;
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        d3dStatus = RenderDevice.Initialize(Window.Handle(), Window.Width(), Window.Height(), Log);
    }
    if (!d3dStatus.IsOk()) { return d3dStatus; }
    RenderResources = &resources;
    D3DInitialized.store(true);
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        Ui.SetStage("preloading ui textures", 0.34f);
        RenderDevice.PreloadUiTextures(resources, Ui, Log);
    }
    RenderDevice.InspectShaderResources(resources, Log);
    RepaintDirty = true;
    return FStatus::Ok();
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
    std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
    std::lock_guard<std::mutex> renderLock(RenderMutex);
    FStatus status = RenderDevice.RenderUiDesktop(*RenderResources, Ui, client, Log);
    if (!status.IsOk() && Log) { Log->Warning("D3D9 render failed: " + status.Message()); }
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
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            inputChanged = Ui.HandleInputFrame(input, client, Log);
            if (inputChanged) {
                const std::string& action = Ui.ActionState().LastAction;
                if (!action.empty() && action != LastUiAction) { LastUiAction = action; Ui.AddStatusLine("ui: " + action); }
            }
        }
        if (LastUiAction == "quit_requested") { Shutdown(); break; }
        if (inputChanged) { RepaintDirty = true; }
        {
            std::lock_guard<std::mutex> lock(SessionMutex);
            Session.Tick();
        }
        UpdateStageFromSession();
        auto now = std::chrono::steady_clock::now();
        if (D3DInitialized.load() && (RepaintDirty || now - lastRender >= std::chrono::milliseconds(250))) { RenderFrame(); lastRender = now; RepaintDirty = false; }
        else if (RepaintDirty) { RequestRepaintThrottled(); }
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
    if (!ShellCreated || D3DInitialized.load()) { return; }
    auto now = std::chrono::steady_clock::now();
    if (RepaintDirty || now - LastPaint > std::chrono::milliseconds(50)) { Window.RequestRepaint(); LastPaint = now; }
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
    std::lock_guard<std::recursive_mutex> lock(UiMutex);
    if (stage == EClientSessionStage::ProbeReceiving) { Ui.SetStage(stageText, 0.86f); RepaintDirty = true; }
    else if (stage == EClientSessionStage::Connected) { Ui.SetStage("connected; awaiting protocol", 0.80f); RepaintDirty = true; }
    else if (stage == EClientSessionStage::Failed) { Ui.SetStage("network probe failed", 0.70f); RepaintDirty = true; }
}
}
