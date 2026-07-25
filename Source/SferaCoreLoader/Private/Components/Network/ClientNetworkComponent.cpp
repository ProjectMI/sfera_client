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
    if (CharacterBusy.load()) { LoginBusy.store(false); return false; }
    const std::optional<FEndpoint> endpoint = Endpoint();
    if (!endpoint) { LoginBusy.store(false); return false; }
    StopWorldEventPump();
    if (LoginThread.joinable()) { LoginThread.join(); }
    if (CharacterThread.joinable()) { CharacterThread.join(); }
    std::shared_ptr<FServerSession> previousSession;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        previousSession = std::move(ActiveSession);
        PendingLogin.reset();
        PendingCharacter.reset();
        LoginRules = rules;
    }
    if (previousSession) { previousSession->Close(); }
    ServerEvents.Clear();
    LoginThread = std::thread([this, endpoint = *endpoint, login = std::move(login), password = std::move(password), rules, timeoutMs]() mutable
    {
        FLoginProbeResult result = ProbeLoginServer(endpoint, login, password, rules, timeoutMs);
        ServerEvents.Push(PacketDispatcher.BuildLoginSnapshot(result));
        FLoginNetworkEvent event;
        event.Connected = result.Connected;
        event.CharacterSelectReady = result.CharacterSelectReady;
        event.Message = result.Message;
        {
            std::lock_guard<std::mutex> lock(StateMutex);
            ActiveSession = result.CharacterSelectReady && result.Session ? result.Session : nullptr;
            PendingLogin = std::move(event);
        }
        LoginBusy.store(false);
    });
    return true;
}

std::optional<FLoginNetworkEvent> FClientNetworkComponent::PollLogin()
{
    std::lock_guard<std::mutex> lock(StateMutex);
    if (!PendingLogin) { return std::nullopt; }
    std::optional<FLoginNetworkEvent> result = std::move(PendingLogin);
    PendingLogin.reset();
    return result;
}

bool FClientNetworkComponent::BeginCharacterEnter(int32 slot, int32 timeoutMs)
{
    return LaunchCharacterTask(ECharacterNetworkAction::Enter, slot, [slot, timeoutMs](const std::shared_ptr<FServerSession>& session)
    {
        FCharacterActionResult selected = session->SelectCharacter(slot, timeoutMs);
        if (!selected.Ok) { return selected; }
        FCharacterActionResult result = session->SendIngameAck(timeoutMs);
        result.Frames.insert(result.Frames.end(), selected.Frames.begin(), selected.Frames.end());
        FCharacterActionResult lateWorld = session->PollFrames(32);
        result.Frames.insert(result.Frames.end(), lateWorld.Frames.begin(), lateWorld.Frames.end());
        result.PacketCount += selected.PacketCount + lateWorld.PacketCount;
        result.ByteCount += selected.ByteCount + lateWorld.ByteCount;
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
    return LaunchCharacterTask(ECharacterNetworkAction::Create, slot, [slot, name = std::move(name), appearance, timeoutMs](const std::shared_ptr<FServerSession>& session)
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
    return LaunchCharacterTask(ECharacterNetworkAction::Delete, slot, [slot, timeoutMs](const std::shared_ptr<FServerSession>& session)
    {
        return session->DeleteCharacter(slot, timeoutMs);
    }, std::move(refresh));
}

bool FClientNetworkComponent::LaunchCharacterTask(ECharacterNetworkAction action, int32 slotContext, std::function<FCharacterActionResult(const std::shared_ptr<FServerSession>&)> task, std::optional<FRefreshCredentials> refresh)
{
    if (CharacterBusy.exchange(true)) { return false; }
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    if (!session || !session->Connected()) { CharacterBusy.store(false); return false; }
    StopWorldEventPump();
    if (CharacterThread.joinable()) { CharacterThread.join(); }
    CharacterThread = std::thread([this, action, slotContext, session, task = std::move(task), refresh = std::move(refresh)]() mutable
    {
        FCharacterNetworkEvent event;
        event.Action = action;
        FCharacterActionResult result = task(session);
        event.Ok = result.Ok;
        event.Message = result.Message;
        if (result.Ok && action == ECharacterNetworkAction::Enter)
        {
            ServerEvents.Push(PacketDispatcher.BuildCharacterActivated(slotContext, std::nullopt));
        }
        DispatchFrames(result.Frames, session->LocalId());
        std::shared_ptr<FServerSession> refreshedSession;
        if (result.Ok && refresh)
        {
            event.RefreshAttempted = true;
            session->Close();
            FLoginProbeResult refreshed = RefreshCharacterSelectSession(*refresh);
            event.RefreshReady = refreshed.CharacterSelectReady && static_cast<bool>(refreshed.Session);
            event.RefreshMessage = refreshed.Message;
            if (event.RefreshReady)
            {
                refreshedSession = refreshed.Session;
                ServerEvents.Push(PacketDispatcher.BuildLoginSnapshot(refreshed));
            }
            else { ServerEvents.Push(PacketDispatcher.BuildSessionClosed("character session refresh failed")); }
        }
        {
            std::lock_guard<std::mutex> lock(StateMutex);
            if (action != ECharacterNetworkAction::Enter) { ActiveSession = std::move(refreshedSession); }
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
    return event;
}

void FClientNetworkComponent::DispatchFrames(const std::vector<std::vector<uint8>>& frames, uint16 localEntityId)
{
    if (frames.empty()) { return; }
    ServerEvents.Push(PacketDispatcher.DecodeFrames(frames, FPacketDispatchContext{localEntityId}));
}

std::vector<FServerEvent> FClientNetworkComponent::DrainServerEvents()
{
    return ServerEvents.Drain();
}

void FClientNetworkComponent::StartWorldEventPump()
{
    if (WorldPumpRunning.exchange(true)) { return; }
    if (WorldThread.joinable()) { WorldThread.join(); }
    WorldStopRequested.store(false);
    WorldThread = std::thread([this]()
    {
        bool connectionLost = false;
        while (!WorldStopRequested.load())
        {
            const std::shared_ptr<FServerSession> session = GetActiveSession();
            if (!session || !session->Connected()) { connectionLost = true; break; }
            FCharacterActionResult result = session->PollFrames(64);
            DispatchFrames(result.Frames, session->LocalId());
            if (result.Disconnected) { session->Close(); connectionLost = true; break; }
            if (result.Frames.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        }
        if (connectionLost && !WorldStopRequested.load()) { ServerEvents.Push(PacketDispatcher.BuildSessionClosed("world session disconnected")); }
        WorldPumpRunning.store(false);
    });
}

void FClientNetworkComponent::StopWorldEventPump()
{
    WorldStopRequested.store(true);
    if (WorldThread.joinable()) { WorldThread.join(); }
    WorldPumpRunning.store(false);
}

bool FClientNetworkComponent::SendChatMessage(uint8 channel, std::string_view sender, std::string_view text)
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->SendChatMessage(channel, sender, text);
}

bool FClientNetworkComponent::SendWorldPosition(double x, double y, double z, double angle)
{
    const std::shared_ptr<FServerSession> session = GetActiveSession();
    return session && session->SendWorldPosition(x, y, z, angle);
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

std::shared_ptr<FServerSession> FClientNetworkComponent::GetActiveSession() const
{
    std::lock_guard<std::mutex> lock(StateMutex);
    return ActiveSession;
}

void FClientNetworkComponent::CloseActiveSession()
{
    StopWorldEventPump();
    if (CharacterThread.joinable()) { CharacterThread.join(); }
    std::shared_ptr<FServerSession> session;
    {
        std::lock_guard<std::mutex> lock(StateMutex);
        session = std::move(ActiveSession);
    }
    if (session)
    {
        session->Close();
        ServerEvents.Push(PacketDispatcher.BuildSessionClosed("active session closed"));
    }
    CharacterBusy.store(false);
}

void FClientNetworkComponent::Shutdown()
{
    StopWorldEventPump();
    if (LoginThread.joinable()) { LoginThread.join(); }
    CloseActiveSession();
    std::lock_guard<std::mutex> lock(StateMutex);
    PendingLogin.reset();
    PendingCharacter.reset();
    LoginBusy.store(false);
}
