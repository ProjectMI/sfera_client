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
    windowDesc.Windowed = false;
    windowDesc.Width = GetSystemMetrics(SM_CXSCREEN);
    windowDesc.Height = GetSystemMetrics(SM_CYSCREEN);
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

void FClientFrontendRuntime::ConfigureNetwork(const FClientFrontendDesc& desc) {
    NetworkDesc = desc;
    NetworkConfigured = true;
    NetworkProbeStarted = false;
    {
        std::lock_guard<std::mutex> lock(SessionMutex);
        Session.Configure(desc.Endpoint);
    }
    AddStatusLine(desc.Endpoint ? "network: endpoint ready; press OK to connect" : "network: endpoint missing; press OK will report error");
}

void FClientFrontendRuntime::StartNetworkProbeAfterMenuOk() {
    if (NetworkProbeStarted) { return; }
    NetworkProbeStarted = true;
    if (!NetworkConfigured) { AddStatusLine("network: endpoint is not configured"); return; }
    if (!NetworkDesc.TryNetworkProbe || !NetworkDesc.Endpoint) { AddStatusLine("network: probe skipped"); return; }
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.ShowNextPageLoading("connecting to " + NetworkDesc.Endpoint->Host + ":" + std::to_string(NetworkDesc.Endpoint->Port));
    }
    AddStatusLine("network: connecting after OK");
    RepaintDirty = true;
    NetworkConnectInFlight.store(true);
    FClientFrontendDesc connectDesc = NetworkDesc;
    NetworkThread = std::thread([this, connectDesc]() {
        FStatus connectStatus;
        {
            std::lock_guard<std::mutex> lock(SessionMutex);
            connectStatus = Session.StartProbe(connectDesc.NetworkConnectTimeoutMs);
        }
        if (!connectStatus.IsOk()) { AddStatusLine("network: " + connectStatus.Message()); }
        else {
            AddStatusLine("network: connected, loading next page");
            LoadNextPageAfterConnection();
        }
        NetworkConnectInFlight.store(false);
    });
}


void FClientFrontendRuntime::LoadNextPageAfterConnection() {
    if (!RenderResources) { AddStatusLine("next page: resources unavailable"); return; }
    FStatus loadStatus;
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        Ui.ShowConnectedPage("server connected; loading next page resources");
        loadStatus = Ui.LoadNextPageResources(*RenderResources, Log);
    }
    if (!loadStatus.IsOk()) { AddStatusLine("next page: " + loadStatus.Message()); return; }
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.PreloadUiTextures(*RenderResources, Ui, Log);
        Ui.ShowConnectedPage("next page loaded");
    }
    RepaintDirty = true;
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
                if (!action.empty() && action != LastUiAction) { LastUiAction = action; Ui.AddStatusLine("ui: " + action); HandleUiAction(action); }
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
    if (NetworkThread.joinable()) { NetworkThread.join(); }
    NetworkConnectInFlight.store(false);
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

void FClientFrontendRuntime::HandleUiAction(const std::string& action) {
    if (action == "login_requested") { StartNetworkProbeAfterMenuOk(); }
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
    if (stage == EClientSessionStage::ProbeReceiving) { Ui.ShowConnectedPage(stageText); RepaintDirty = true; }
    else if (stage == EClientSessionStage::Connected) { if (!Ui.IsNextPageReady()) { Ui.ShowConnectedPage("server connected; waiting for next page resources"); } RepaintDirty = true; }
    else if (stage == EClientSessionStage::Failed) { Ui.ShowNextPageLoading("network probe failed: " + stageText); RepaintDirty = true; }
}
}
