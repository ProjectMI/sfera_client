#pragma once
#include "Client/ClientSettings.h"
#include "Core/Logger.h"
#include "Network/ClientSession.h"
#include "Network/LoginClient.h"
#include "Platform/Win32Window.h"
#include "Renderer/D3D9RenderDevice.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiRuntime.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct FClientFrontendDesc 
{
    FClientSettings Settings;
    std::optional<FEndpoint> Endpoint;
    bool TryNetworkProbe = false;
    uint32 NetworkConnectTimeoutMs = 2500;
};

class FClientFrontendRuntime 
{
public:
    explicit FClientFrontendRuntime(FLogger* logger = nullptr);
    ~FClientFrontendRuntime();
    FStatus CreateShell(const FClientSettings& settings);
    void ShowShell();
    void ConfigureNetwork(const FClientFrontendDesc& desc);
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    bool PumpUi();
    FStatus InitializeUiResources(const FResourceManager& resources, const FUiBootstrapDesc& desc);
    FStatus InitializeD3D9(const FResourceManager& resources);
    void RenderFrame();
    void StartNetworkProbe(const FClientFrontendDesc& desc);
    FStatus RunEventLoop();
    void Shutdown();
    bool IsWindowOpen() const { return Window.IsOpen(); }
private:
    void RequestRepaintThrottled();
    void UpdateStageFromSession();
    void ProcessUiAction(const std::string& action);
    void BeginLoginRequest();
    void PollLoginResult();
    void BeginCharacterEnterRequest();
    void BeginCharacterCreateRequest();
    void BeginCharacterDeleteRequest();
    void PollCharacterResult();
    void DrawLoadingFrame(HDC__* dc, const tagRECT& rect);
    void LoadSavedLogin();
    void StoreSavedLogin(bool enabled, const std::string& login, const std::string& password);
    void CloseActiveServerSession();
    FLoginProbeResult RefreshCharacterSelectSession(const std::wstring& login, const std::wstring& password, int32 timeoutMs);
    static std::wstring Utf8ToWide(const std::string& text);
    FLogger* Log = nullptr;
    FWin32Window Window;
    FUiRuntime Ui;
    FD3D9RenderDevice RenderDevice;
    FClientSession Session;
    FClientSettings Settings;
    FCharacterAppearanceRules AppearanceRules;
    const FResourceManager* RenderResources = nullptr;
    mutable std::recursive_mutex UiMutex;
    mutable std::mutex RenderMutex;
    mutable std::mutex SessionMutex;
    mutable std::mutex LoginMutex;
    mutable std::mutex CharacterMutex;
    std::optional<FEndpoint> Endpoint;
    std::optional<FLoginProbeResult> PendingLoginResult;
    std::optional<FCharacterActionResult> PendingCharacterResult;
    std::optional<FLoginProbeResult> PendingCharacterRefreshResult;
    int32 PendingCharacterActionKind = 0;
    int32 PendingCharacterSlot = 0;
    std::wstring PendingCharacterName;
    FCharacterCreationAppearance PendingCharacterAppearance;
    std::shared_ptr<FServerSession> ActiveServerSession;
    std::thread LoginThread;
    std::thread CharacterThread;
    bool ShellCreated = false;
    std::atomic_bool D3DInitialized{false};
    std::atomic_bool LoginInProgress{false};
    std::atomic_bool CharacterActionInProgress{false};
    bool RepaintDirty = true;
    EClientSessionStage LastSessionStage = EClientSessionStage::Idle;
    std::chrono::steady_clock::time_point LastPaint = std::chrono::steady_clock::now();
};