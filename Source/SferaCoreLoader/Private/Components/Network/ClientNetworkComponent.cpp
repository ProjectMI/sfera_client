#include "Components/Network/ClientNetworkComponent.h"

FClientNetworkComponent::FClientNetworkComponent(FLogger* logger) : Log(logger) {}
FClientNetworkComponent::~FClientNetworkComponent() { Shutdown(); }

void FClientNetworkComponent::Configure(std::optional<FEndpoint> endpoint)
{
    std::lock_guard<std::mutex> lock(StateMutex);
    ConfiguredEndpoint = std::move(endpoint);
}

std::optional<FEndpoint> FClientNetworkComponent::Endpoint() const
{
    std::lock_guard<std::mutex> lock(StateMutex);
    return ConfiguredEndpoint;
}

bool FClientNetworkComponent::BeginLogin(std::wstring login, std::wstring password, FCharacterAppearanceRules rules, int32 timeoutMs)
{
    if (login.empty() || password.empty() || LoginBusy.exchange(true)) { return false; }
    const std::optional<FEndpoint> endpoint = Endpoint();
    if (!endpoint) { LoginBusy.store(false); return false; }
    if (LoginThread.joinable()) { LoginThread.join(); }
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        LoginRules = rules;
    }
    LoginThread = std::thread([this, endpoint = *endpoint, login = std::move(login), password = std::move(password), rules, timeoutMs]() mutable
    {
        FLoginProbeResult result = ProbeLoginServer(endpoint, login, password, rules, timeoutMs);
        {
            std::lock_guard<std::mutex> lock(StateMutex);
            PendingLogin = std::move(result);
        }
        LoginBusy.store(false);
    });
    return true;
}

std::optional<FLoginProbeResult> FClientNetworkComponent::PollLogin()
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (!PendingLogin) { return std::nullopt; }
    std::optional<FLoginProbeResult> result = std::move(PendingLogin);
    PendingLogin.reset();
    if (result->CharacterSelectReady && result->Session) { ActiveSession = result->Session; }
    return result;
}

bool FClientNetworkComponent::BeginCharacterEnter(int32 slot, int32 timeoutMs)
{
    return LaunchCharacterTask(ECharacterNetworkAction::Enter, [slot, timeoutMs](const std::shared_ptr<FServerSession>& session)
    {
        FCharacterActionResult selected = session->SelectCharacter(slot, timeoutMs);
        if (!selected.Ok) { return selected; }
        FCharacterActionResult result = session->SendIngameAck(timeoutMs);
        result.Frames.insert(result.Frames.end(), selected.Frames.begin(), selected.Frames.end());
        if (!result.ServerPosition && selected.ServerPosition) { result.ServerPosition = selected.ServerPosition; }
        FCharacterActionResult lateWorld = session->PollFrames(32);
        result.Frames.insert(result.Frames.end(), lateWorld.Frames.begin(), lateWorld.Frames.end());
        result.PacketCount += selected.PacketCount + lateWorld.PacketCount;
        result.ByteCount += selected.ByteCount + lateWorld.ByteCount;
        if (!result.ServerPosition && lateWorld.ServerPosition) { result.ServerPosition = lateWorld.ServerPosition; }
        result.Message = selected.Message + "; " + result.Message + "; late world packets=" + std::to_string(lateWorld.PacketCount);
        return result;
    }, std::nullopt);
}

bool FClientNetworkComponent::BeginCharacterCreate(int32 slot, std::wstring name, FCharacterCreationAppearance appearance, std::wstring login, std::wstring password, int32 timeoutMs)
{
    FCharacterAppearanceRules rules;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        rules = LoginRules;
    }
    FRefreshCredentials refresh{std::move(login), std::move(password), rules, timeoutMs};
    return LaunchCharacterTask(ECharacterNetworkAction::Create, [slot, name = std::move(name), appearance, timeoutMs](const std::shared_ptr<FServerSession>& session)
    {
        return session->CreateCharacter(slot, name, appearance, timeoutMs);
    }, std::move(refresh));
}

bool FClientNetworkComponent::BeginCharacterDelete(int32 slot, std::wstring login, std::wstring password, int32 timeoutMs)
{
    FCharacterAppearanceRules rules;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        rules = LoginRules;
    }
    FRefreshCredentials refresh{std::move(login), std::move(password), rules, timeoutMs};
    return LaunchCharacterTask(ECharacterNetworkAction::Delete, [slot, timeoutMs](const std::shared_ptr<FServerSession>& session)
    {
        return session->DeleteCharacter(slot, timeoutMs);
    }, std::move(refresh));
}

bool FClientNetworkComponent::LaunchCharacterTask(ECharacterNetworkAction action, std::function<FCharacterActionResult(const std::shared_ptr<FServerSession>&)> task, std::optional<FRefreshCredentials> refresh)
{
    if (CharacterBusy.exchange(true)) { return false; }
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    if (!session || !session->Connected()) { CharacterBusy.store(false); return false; }
    if (CharacterThread.joinable()) { CharacterThread.join(); }
    CharacterThread = std::thread([this, action, session, task = std::move(task), refresh = std::move(refresh)]() mutable
    {
        FCharacterNetworkEvent event;
        event.Action = action;
        event.Result = task(session);
        if (event.Result.Ok && refresh)
        {
            session->Close();
            event.RefreshedLogin = RefreshCharacterSelectSession(*refresh);
        }
        {
            std::lock_guard<std::mutex> lock(StateMutex);
            PendingCharacter = std::move(event);
        }
        CharacterBusy.store(false);
    });
    return true;
}

FLoginProbeResult FClientNetworkComponent::RefreshCharacterSelectSession(const FRefreshCredentials& credentials) const
{
    FLoginProbeResult result;
    const std::optional<FEndpoint> endpoint = Endpoint();
    if (!endpoint) { result.Message = "endpoint not configured"; return result; }
    constexpr int delays[] = {150, 350, 700};
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(delays[attempt]));
        result = ProbeLoginServer(*endpoint, credentials.Login, credentials.Password, credentials.Rules, credentials.TimeoutMs);
        if (result.CharacterSelectReady && result.Session)
        {
            result.Message += "; refresh attempt=" + std::to_string(attempt + 1);
            return result;
        }
    }
    return result;
}

std::optional<FCharacterNetworkEvent> FClientNetworkComponent::PollCharacter()
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (!PendingCharacter) { return std::nullopt; }
    std::optional<FCharacterNetworkEvent> event = std::move(PendingCharacter);
    PendingCharacter.reset();
    if (event->Result.Ok && event->Action != ECharacterNetworkAction::Enter)
    {
        if (event->RefreshedLogin && event->RefreshedLogin->CharacterSelectReady && event->RefreshedLogin->Session) { ActiveSession = event->RefreshedLogin->Session; }
        else { ActiveSession.reset(); }
    }
    return event;
}

std::shared_ptr<FServerSession> FClientNetworkComponent::GetActiveSession() const
{
    std::lock_guard<std::mutex> lock(StateMutex);
    return ActiveSession;
}

FCharacterActionResult FClientNetworkComponent::PollWorldFrames(int32 maxFrames)
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->Connected() ? session->PollFrames(maxFrames) : FCharacterActionResult{};
}

bool FClientNetworkComponent::SendChatMessage(uint8 channel, std::string_view text)
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->SendChatMessage(channel, text);
}

bool FClientNetworkComponent::SendStatAllocation(const std::array<int32, 8>& deltas)
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->SendStatAllocation(deltas);
}

bool FClientNetworkComponent::HasActiveSession() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->Connected();
}

bool FClientNetworkComponent::HasGameTime() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->HasGameTime();
}

float FClientNetworkComponent::GameTimeFraction() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->HasGameTime() ? session->GameTimeFraction() : 0.0f;
}

int32 FClientNetworkComponent::GameDay() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session ? session->GameDay() : 0;
}

int32 FClientNetworkComponent::GameMonth() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session ? session->GameMonth() : 0;
}

int32 FClientNetworkComponent::GameYear() const
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session ? session->GameYear() : 0;
}

void FClientNetworkComponent::CloseActiveSession()
{
    if (CharacterThread.joinable()) { CharacterThread.join(); }
    std::shared_ptr<FServerSession> session;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        session = std::move(ActiveSession);
    }
    if (session) { session->Close(); }
    CharacterBusy.store(false);
}

void FClientNetworkComponent::Shutdown()
{
    if (LoginThread.joinable()) { LoginThread.join(); }
    CloseActiveSession();
    std::lock_guard<std::mutex> lock(StateMutex);
    PendingLogin.reset();
    PendingCharacter.reset();
    LoginBusy.store(false);
}
