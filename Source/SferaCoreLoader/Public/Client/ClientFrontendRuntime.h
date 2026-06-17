#pragma once
#include "Core/Logger.h"
#include "Network/ClientSession.h"
#include "Platform/Win32Window.h"
#include "Renderer/D3D9RenderDevice.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/LoadingScreen.h"
#include "UI/UiResourceDocument.h"
#include <chrono>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Sfera {
struct FClientFrontendDesc {
    std::optional<FEndpoint> Endpoint;
    std::vector<FEndpoint> EndpointCandidates;
    bool TryNetworkProbe = true;
    uint32 NetworkConnectTimeoutMs = 5000;
};

class FClientFrontendRuntime {
public:
    explicit FClientFrontendRuntime(FLogger* logger = nullptr);
    ~FClientFrontendRuntime();

    FStatus CreateShell();
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    bool PumpUi();
    void InitializeLoadingResources(const FResourceManager& resources);
    void InitializeD3D9(const FResourceManager& resources);
    void RenderFrame();
    void StartNetworkProbe(const FClientFrontendDesc& desc);
    FStatus RunEventLoop();
    void Shutdown();
    bool IsWindowOpen() const { return Window.IsOpen(); }

private:
    void RequestRepaintThrottled();
    void UpdateStageFromSession();
    bool HasD3D() const;

    FLogger* Log = nullptr;
    FWin32Window Window;
    FLoadingScreenModel LoadingModel;
    FUiResourceSystem UiResources;
    FD3D9RenderDevice RenderDevice;
    FClientSession Session;
    const FResourceManager* RenderResources = nullptr;
    mutable std::recursive_mutex ModelMutex;
    mutable std::mutex RenderMutex;
    mutable std::mutex SessionMutex;
    bool ShellCreated = false;
    std::atomic_bool D3DInitialized{false};
    bool RepaintDirty = true;
    EClientSessionStage LastSessionStage = EClientSessionStage::Idle;
    std::string LastUiAction;
    std::chrono::steady_clock::time_point LastPaint = std::chrono::steady_clock::now();
};
}
