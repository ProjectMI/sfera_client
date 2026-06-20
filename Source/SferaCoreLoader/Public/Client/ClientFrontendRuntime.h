#pragma once
#include "Core/Logger.h"
#include "Network/ClientSession.h"
#include "Platform/Win32Window.h"
#include "Renderer/D3D9RenderDevice.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiRuntime.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Sfera {
struct FClientFrontendDesc {
    std::optional<FEndpoint> Endpoint;
    bool TryNetworkProbe = true;
    uint32 NetworkConnectTimeoutMs = 350;
};

class FClientFrontendRuntime {
public:
    explicit FClientFrontendRuntime(FLogger* logger = nullptr);
    ~FClientFrontendRuntime();
    FStatus CreateShell();
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    bool PumpUi();
    FStatus InitializeUiResources(const FResourceManager& resources);
    FStatus InitializeD3D9(const FResourceManager& resources);
    void RenderFrame();
    void ConfigureNetwork(const FClientFrontendDesc& desc);
    void StartNetworkProbeAfterMenuOk();
    FStatus RunEventLoop();
    void Shutdown();
    bool IsWindowOpen() const { return Window.IsOpen(); }
private:
    void RequestRepaintThrottled();
    void UpdateStageFromSession();
    void HandleUiAction(const std::string& action);
    FLogger* Log = nullptr;
    FWin32Window Window;
    FUiRuntime Ui;
    FD3D9RenderDevice RenderDevice;
    FClientSession Session;
    const FResourceManager* RenderResources = nullptr;
    mutable std::recursive_mutex UiMutex;
    mutable std::mutex RenderMutex;
    mutable std::mutex SessionMutex;
    bool ShellCreated = false;
    std::atomic_bool D3DInitialized{false};
    bool RepaintDirty = true;
    FClientFrontendDesc NetworkDesc;
    bool NetworkConfigured = false;
    bool NetworkProbeStarted = false;
    std::atomic_bool NetworkConnectInFlight{false};
    std::thread NetworkThread;
    EClientSessionStage LastSessionStage = EClientSessionStage::Idle;
    std::string LastUiAction;
    std::chrono::steady_clock::time_point LastPaint = std::chrono::steady_clock::now();
};
}
