#pragma once
#include "Components/Network/GamePacketDispatcher.h"
#include "Components/Network/ServerEventQueue.h"
#include "Core/Logger.h"
#include "Network/LoginClient.h"

enum class ECharacterNetworkAction
{
    Enter,
    Create,
    Delete
};

struct FLoginNetworkEvent
{
    bool Connected = false;
    bool CharacterSelectReady = false;
    std::string Message;
};

struct FCharacterNetworkEvent
{
    ECharacterNetworkAction Action = ECharacterNetworkAction::Enter;
    bool Ok = false;
    std::string Message;
    bool RefreshAttempted = false;
    bool RefreshReady = false;
    std::string RefreshMessage;
};

class FClientNetworkComponent
{
public:
    explicit FClientNetworkComponent(FLogger* logger = nullptr);
    ~FClientNetworkComponent();
    void Configure(std::optional<FEndpoint> endpoint);
    std::optional<FEndpoint> Endpoint() const;
    bool BeginLogin(std::wstring login, std::wstring password, FCharacterAppearanceRules rules, int32 timeoutMs = 2500);
    std::optional<FLoginNetworkEvent> PollLogin();
    bool BeginCharacterEnter(int32 slot, int32 timeoutMs = 2500);
    bool BeginCharacterCreate(int32 slot, std::wstring name, FCharacterCreationAppearance appearance, std::wstring login, std::wstring password, int32 timeoutMs = 2500);
    bool BeginCharacterDelete(int32 slot, std::wstring login, std::wstring password, int32 timeoutMs = 2500);
    std::optional<FCharacterNetworkEvent> PollCharacter();
    std::vector<FServerEvent> DrainServerEvents();
    void StartWorldEventPump();
    void StopWorldEventPump();
    bool SendChatMessage(uint8 channel, std::string_view text);
    bool SendStatAllocation(const std::array<int32, 8>& deltas);
    bool HasActiveSession() const;
    void CloseActiveSession();
    void Shutdown();
private:
    struct FRefreshCredentials
    {
        std::wstring Login;
        std::wstring Password;
        FCharacterAppearanceRules Rules;
        int32 TimeoutMs = 2500;
    };
    bool LaunchCharacterTask(ECharacterNetworkAction action, int32 slotContext, std::function<FCharacterActionResult(const std::shared_ptr<FServerSession>&)> task, std::optional<FRefreshCredentials> refresh);
    FLoginProbeResult RefreshCharacterSelectSession(const FRefreshCredentials& credentials) const;
    std::shared_ptr<FServerSession> GetActiveSession() const;
    void DispatchFrames(const std::vector<std::vector<uint8>>& frames, uint16 localEntityId);
    FLogger* Log = nullptr;
    std::optional<FEndpoint> ConfiguredEndpoint;
    FCharacterAppearanceRules LoginRules;
    std::shared_ptr<FServerSession> ActiveSession;
    FGamePacketDispatcher PacketDispatcher;
    FServerEventQueue ServerEvents;
    std::thread LoginThread;
    std::thread CharacterThread;
    std::thread WorldThread;
    mutable std::mutex StateMutex;
    std::optional<FLoginNetworkEvent> PendingLogin;
    std::optional<FCharacterNetworkEvent> PendingCharacter;
    std::atomic_bool LoginBusy{false};
    std::atomic_bool CharacterBusy{false};
    std::atomic_bool WorldStopRequested{true};
    std::atomic_bool WorldPumpRunning{false};
};
