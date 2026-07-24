#pragma once
#include "Core/Logger.h"
#include "Network/LoginClient.h"

enum class ECharacterNetworkAction
{
    Enter,
    Create,
    Delete
};

struct FCharacterNetworkEvent
{
    ECharacterNetworkAction Action = ECharacterNetworkAction::Enter;
    FCharacterActionResult Result;
    std::optional<FLoginProbeResult> RefreshedLogin;
};

class FClientNetworkComponent
{
public:
    explicit FClientNetworkComponent(FLogger* logger = nullptr);
    ~FClientNetworkComponent();
    void Configure(std::optional<FEndpoint> endpoint);
    std::optional<FEndpoint> Endpoint() const;
    bool BeginLogin(std::wstring login, std::wstring password, FCharacterAppearanceRules rules, int32 timeoutMs = 2500);
    std::optional<FLoginProbeResult> PollLogin();
    bool BeginCharacterEnter(int32 slot, int32 timeoutMs = 2500);
    bool BeginCharacterCreate(int32 slot, std::wstring name, FCharacterCreationAppearance appearance, std::wstring login, std::wstring password, int32 timeoutMs = 2500);
    bool BeginCharacterDelete(int32 slot, std::wstring login, std::wstring password, int32 timeoutMs = 2500);
    std::optional<FCharacterNetworkEvent> PollCharacter();
    FCharacterActionResult PollWorldFrames(int32 maxFrames = 32);
    bool SendChatMessage(uint8 channel, std::string_view text);
    bool SendStatAllocation(const std::array<int32, 8>& deltas);
    bool HasActiveSession() const;
    bool HasGameTime() const;
    float GameTimeFraction() const;
    int32 GameDay() const;
    int32 GameMonth() const;
    int32 GameYear() const;
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
    bool LaunchCharacterTask(ECharacterNetworkAction action, std::function<FCharacterActionResult(const std::shared_ptr<FServerSession>&)> task, std::optional<FRefreshCredentials> refresh);
    FLoginProbeResult RefreshCharacterSelectSession(const FRefreshCredentials& credentials) const;
    std::shared_ptr<FServerSession> GetActiveSession() const;
    FLogger* Log = nullptr;
    std::optional<FEndpoint> ConfiguredEndpoint;
    FCharacterAppearanceRules LoginRules;
    std::shared_ptr<FServerSession> ActiveSession;
    std::thread LoginThread;
    std::thread CharacterThread;
    mutable std::mutex StateMutex;
    std::optional<FLoginProbeResult> PendingLogin;
    std::optional<FCharacterNetworkEvent> PendingCharacter;
    std::atomic_bool LoginBusy{false};
    std::atomic_bool CharacterBusy{false};
};
