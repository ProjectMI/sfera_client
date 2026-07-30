#include "Client/ClientFrontendRuntime.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"

FClientFrontendRuntime::FClientFrontendRuntime(FLogger* Logger) : Log(Logger), NetworkComponent(Logger)
{
    RemoteAppearanceHints.reserve(256);
    RemotePlayerNames.reserve(1024);
    RemotePlayerEntities.reserve(256);
}
FClientFrontendRuntime::~FClientFrontendRuntime()
{
    Shutdown();
}

std::wstring FClientFrontendRuntime::Utf8ToWide(const std::string& text)
{
    return Common::Utf8ToWide(text);
}

namespace
{
    float SecondsBetween(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
    {
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        return static_cast<float>(microseconds) / 1000000.0f;
    }

    float ClampFrameDelta(float seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0.0f)
        {
            return 0.0f;
        }

        return std::clamp(seconds, 0.0f, 0.1f);
    }

    std::filesystem::path SavedLoginPath()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        std::filesystem::path root = count > 0 ? std::filesystem::path(buffer.data()).parent_path() : std::filesystem::current_path();
        return root / "sfera_login.cache";
    }

    bool SameReportedPosition(const FGameWorldPosition& left, const FGameWorldPosition& right)
    {
        const double angleDelta = std::remainder(left.Angle - right.Angle, 2.0 * std::acos(-1.0));
        return std::abs(left.X - right.X) <= 0.02 && std::abs(left.Y - right.Y) <= 0.02 && std::abs(left.Z - right.Z) <= 0.02 && std::abs(angleDelta) <= 0.005;
    }

    FRemoteGamePlayer MakeRemotePlayer(uint64 entityId, std::string name, const FServerWorldPosition& position, const FCharacterCreationAppearance* appearance = nullptr)
    {
        FRemoteGamePlayer player;
        player.EntityId = entityId;
        player.Name = std::move(name);
        player.Position = FGameWorldPosition{position.X, position.Y, position.Z, position.Angle};
        if (appearance) { player.Appearance = *appearance; }
        return player;
    }

    FRemoteGameActor MakeRemoteActor(const FRemoteActorSpawnEvent& event)
    {
        FRemoteGameActor actor;
        actor.EntityId = event.EntityId;
        actor.ObjectType = event.ObjectType;
        actor.ModelTypeCandidates = event.ModelTypeCandidates;
        actor.Position = FGameWorldPosition{event.Position.X, event.Position.Y, event.Position.Z, event.Position.Angle};
        return actor;
    }

    constexpr char kAppearanceSenderPrefix = '~';
    constexpr char kAppearanceSenderDelimiter = '|';

    char AppearanceCode(int32 value)
    {
        return static_cast<char>('A' + std::clamp(value, 0, 15));
    }

    bool SameAppearance(const FCharacterCreationAppearance& left, const FCharacterCreationAppearance& right)
    {
        return left.Female == right.Female && left.ModelBase == right.ModelBase && left.Face == right.Face && left.Hair == right.Hair && left.HairColor == right.HairColor && left.Tattoo == right.Tattoo;
    }

    std::string BuildAppearanceSender(const std::wstring& characterName, const FCharacterCreationAppearance& appearance)
    {
        const FByteArray nameBytes = Common::WideToCp1251Bytes(characterName);
        if (nameBytes.empty() || nameBytes.size() > 19) { return {}; }
        const int32 face = std::clamp(appearance.Face, 0, appearance.Female ? 11 : 12);
        const int32 hair = std::clamp(appearance.Hair, 0, appearance.Female ? 4 : 2);
        const int32 hairColor = std::clamp(appearance.HairColor, 0, 3);
        const int32 tattoo = std::clamp(appearance.Tattoo, 0, 3);
        const std::string nameUtf8 = Common::WideToUtf8(characterName);
        if (nameUtf8.empty()) { return {}; }
        std::string sender;
        sender.reserve(nameUtf8.size() + 7);
        sender.push_back(kAppearanceSenderPrefix);
        sender += nameUtf8;
        sender.push_back(kAppearanceSenderDelimiter);
        sender.push_back(AppearanceCode(appearance.Female ? 1 : 0));
        sender.push_back(AppearanceCode(face));
        sender.push_back(AppearanceCode(hair));
        sender.push_back(AppearanceCode(hairColor));
        sender.push_back(AppearanceCode(tattoo));
        return sender;
    }

}

void FClientFrontendRuntime::DrawLoadingFrame(HDC dc, const RECT& rect)
{
    if (!dc) { return; }

    RECT r
    {
        rect.left, rect.top, rect.right, rect.bottom
    };
    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    std::string stage = "loading";
    float progress = 0.0f;
    std::vector<std::string> lines;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        stage = Ui.Stage();
        progress = Ui.Progress();
        lines = Ui.StatusLines();
    }
    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const int barW = std::max(320, width / 2);
    const int barH = 18;
    const int x = (width - barW) / 2;
    const int y = height / 2 + 28;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(237, 208, 161));
    HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Tahoma");
    HGDIOBJ oldFont = SelectObject(dc, font);
    const std::wstring title = L"Sphere";
    TextOutW(dc, x, y - 56, title.c_str(), static_cast<int>(title.size()));
    const std::wstring wideStage = Utf8ToWide(stage);
    TextOutW(dc, x, y - 30, wideStage.c_str(), static_cast<int>(wideStage.size()));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(118, 92, 65));
    HGDIOBJ oldPen = SelectObject(dc, border);
    HBRUSH empty = CreateSolidBrush(RGB(20, 16, 12));
    HGDIOBJ oldBrush = SelectObject(dc, empty);
    Rectangle(dc, x, y, x + barW, y + barH);
    SelectObject(dc, oldBrush);
    DeleteObject(empty);
    const int fillW = std::clamp(static_cast<int>(barW * std::clamp(progress, 0.0f, 1.0f)), 0, barW);
    HBRUSH fill = CreateSolidBrush(RGB(190, 145, 82));
    RECT fr
    {
        x + 2, y + 2, x + std::max(2, fillW) - 2, y + barH - 2
    };

    if (fillW > 4)
    {
        FillRect(dc, &fr, fill);
    }

    DeleteObject(fill);
    int lineY = y + 34;

    for (const auto& line : lines)
    {
        const std::wstring w = Utf8ToWide(line);
        TextOutW(dc, x, lineY, w.c_str(), static_cast<int>(w.size()));
        lineY += 20;
    }

    SelectObject(dc, oldPen);
    DeleteObject(border);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void FClientFrontendRuntime::LoadSavedLogin()
{
    std::ifstream input(SavedLoginPath(), std::ios::binary);

    if (!input) { return; }

    std::string login;
    std::string password;
    std::getline(input, login);
    std::getline(input, password);

    if (!login.empty())
    {
        Ui.SetLoginCredentials(login, password, true);
        AddStatusLine("login: saved credentials loaded");
    }
}

void FClientFrontendRuntime::StoreSavedLogin(bool enabled, const std::string& login, const std::string& password)
{
    const auto path = SavedLoginPath();

    if (!enabled) { std::error_code ec; std::filesystem::remove(path, ec); return; }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (output)
    {
        output << login << '\n' << password << '\n';
    }
}

void FClientFrontendRuntime::CloseActiveServerSession()
{
    NetworkComponent.CloseActiveSession();
    SetMusicCue("mounts_1d");
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.ClearRemoteGamePlayers();
    }
    LastReportedWorldPosition.reset();
    LastAudioListenerPosition.reset();
    LastWorldPositionReportTime = {};
    NextWorldPositionReportTime = {};
    PendingWorldPositionWarmupReports = 0;
    LocalCharacterName.clear();
    LocalCharacterAppearance = {};
    LocalCharacterIdentityValid = false;
    RemoteAppearanceHints.clear();
    RemotePlayerNames.clear();
    RemotePlayerEntities.clear();
    NextAppearanceAnnouncementTime = {};
    PendingAppearanceAnnouncements = 0;
    ProcessServerEvents();
}



FStatus FClientFrontendRuntime::CreateShell(const FClientSettings& settings)
{
    if (ShellCreated) { return FStatus::Ok(); }

    Settings = settings;
    FWindowDesc windowDesc;
    windowDesc.Width = Settings.Width;
    windowDesc.Height = Settings.Height;
    windowDesc.Borderless = true;
    windowDesc.Title = Settings.Title;
    FStatus status = Window.Create(windowDesc, Log);

    if (!status.IsOk()) { return status; }

    Window.SetPaintCallback([this](HDC dc, const RECT& rect)
    {
        DrawLoadingFrame(dc, rect);
    });
    ShellCreated = true;

    if (Log)
    {
        Log->Info("frontend shell created");
    }

    return FStatus::Ok();
}

void FClientFrontendRuntime::ShowShell()
{
    if (ShellCreated)
    {
        Window.Show();
        Window.ClearCloseRequest();
        RepaintDirty = true;
        Window.RequestRepaint();
    }
}

void FClientFrontendRuntime::ConfigureNetwork(const FClientFrontendDesc& desc)
{
    Settings = desc.Settings;
    NetworkConnectTimeoutMs = desc.NetworkConnectTimeoutMs;
    NetworkComponent.Configure(desc.Endpoint);
    if (desc.Endpoint) { AddStatusLine("network: endpoint configured " + desc.Endpoint->Host + ":" + std::to_string(desc.Endpoint->Port)); }
    else { AddStatusLine("network: endpoint not found"); }
}

void FClientFrontendRuntime::SetStage(std::string stage, float progress)
{
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetStage(std::move(stage), progress);

        if (Log)
        {
            Log->Info("frontend stage: " + Ui.Stage() + ", progress=" + std::to_string(Ui.Progress()));
        }
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

void FClientFrontendRuntime::AddStatusLine(std::string line)
{
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.AddStatusLine(std::move(line));
    }
    RepaintDirty = true;
    RequestRepaintThrottled();
}

bool FClientFrontendRuntime::PumpUi()
{
    if (!ShellCreated)
    {
        return true;
    }

    bool open = Window.PumpMessages();
    return open && Window.IsOpen();
}


FStatus FClientFrontendRuntime::InitializeAudio(const FResourceManager& resources, float soundVolume, float musicVolume)
{
    FStatus status = Audio.Initialize(resources, Log);
    if (!status.IsOk()) { return status; }
    Settings.SoundVolume = std::clamp(soundVolume, 0.0f, 1.0f);
    Settings.MusicVolume = std::clamp(musicVolume, 0.0f, 1.0f);
    ApplyAudioVolumes(Settings.SoundVolume, Settings.MusicVolume);
    SetMusicCue("mounts_1d", 0.25f);
    AddStatusLine("audio: miniaudio runtime ready");
    return status;
}

FStatus FClientFrontendRuntime::InitializeUiResources(const FResourceManager& TerrainResources, const FUiBootstrapDesc& desc)
{
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }

    std::lock_guard<std::recursive_mutex> lock(UiMutex);
    FStatus status = Ui.Initialize(TerrainResources, desc, Log);

    if (!status.IsOk()) { return status; }

    Ui.Character().SetCharacterAppearanceRules(AppearanceRules);
    LoadSavedLogin();
    Ui.SetStage("login screen ready", 1.0f);
    RepaintDirty = true;
    return FStatus::Ok();
}

FStatus FClientFrontendRuntime::InitializeD3D9(const FResourceManager& TerrainResources, const FWorldScene* worldScene)
{
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }

    if (D3DInitialized.load()) { return FStatus::Ok(); }

    FStatus d3dStatus;
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        d3dStatus = RenderDevice.Initialize(Window.Handle(), Window.Width(), Window.Height(), Log);
    }

    if (!d3dStatus.IsOk()) { return d3dStatus; }

    RenderResources = &TerrainResources;
    RenderWorldScene = worldScene;
    D3DInitialized.store(true);
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.PreloadUiTextures(TerrainResources, Ui, Log);
    }
    RepaintDirty = true;
    return FStatus::Ok();
}

void FClientFrontendRuntime::StartNetworkProbe(const FClientFrontendDesc& desc)
{
    ConfigureNetwork(desc);

    if (desc.TryNetworkProbe)
    {
        BeginLoginRequest();
    }
}

void FClientFrontendRuntime::RenderFrame(float deltaSeconds, FGameMovementInput gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested)
{
    if (!ShellCreated || !Window.Handle()) { return; }

    if (!D3DInitialized.load() || !RenderResources) { return; }

    RECT client{};
    GetClientRect(Window.Handle(), &client);
    FStatus status;
    std::optional<FGameWorldPosition> currentWorldPosition;
    float cameraFacing = 0.0f;
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        cameraFacing = RenderDevice.GameWorldCameraFacing();
        Ui.SetCompassHeading(cameraFacing);
        Ui.Tick(deltaSeconds);
        status = RenderDevice.RenderUiDesktop(*RenderResources, RenderWorldScene, Ui, client, deltaSeconds, gameInput, lookDeltaX, lookDeltaY, jumpRequested, Log);
        if (Ui.Mode() == EUiRuntimeMode::Game) { currentWorldPosition = RenderDevice.CurrentGameWorldPosition(); }
    }
    if (currentWorldPosition)
    {
        FAudioListenerState listener;
        listener.Position = {static_cast<float>(currentWorldPosition->X), static_cast<float>(currentWorldPosition->Y), static_cast<float>(currentWorldPosition->Z)};
        listener.Forward = {std::sin(cameraFacing), 0.0f, std::cos(cameraFacing)};
        if (LastAudioListenerPosition && deltaSeconds > 0.0001f)
        {
            const float vx = (listener.Position.X - LastAudioListenerPosition->X) / deltaSeconds;
            const float vy = (listener.Position.Y - LastAudioListenerPosition->Y) / deltaSeconds;
            const float vz = (listener.Position.Z - LastAudioListenerPosition->Z) / deltaSeconds;
            if (vx * vx + vy * vy + vz * vz <= 2500.0f) { listener.Velocity = {vx, vy, vz}; }
        }
        LastAudioListenerPosition = listener.Position;
        Audio.SetListener(listener);
    }
    else { LastAudioListenerPosition.reset(); }

    TrySendAppearanceAnnouncement();

    const auto musicNow = std::chrono::steady_clock::now();
    if (GameState.Session().Connected && GameState.Session().InWorld && (NextWorldMusicEvaluationTime.time_since_epoch().count() == 0 || musicNow >= NextWorldMusicEvaluationTime))
    {
        NextWorldMusicEvaluationTime = musicNow + std::chrono::seconds(1);
        UpdateWorldMusic();
    }
    else if (!GameState.Session().InWorld) { NextWorldMusicEvaluationTime = {}; }

    if (currentWorldPosition && NetworkComponent.HasActiveSession())
    {
        const auto now = std::chrono::steady_clock::now();
        const bool changed = !LastReportedWorldPosition || !SameReportedPosition(*LastReportedWorldPosition, *currentWorldPosition);
        const bool heartbeatDue = LastWorldPositionReportTime.time_since_epoch().count() == 0 || now - LastWorldPositionReportTime >= std::chrono::seconds(2);
        const bool warmupDue = PendingWorldPositionWarmupReports > 0;
        const bool rateReady = NextWorldPositionReportTime.time_since_epoch().count() == 0 || now >= NextWorldPositionReportTime;
        if (rateReady && (changed || heartbeatDue || warmupDue) && NetworkComponent.SendWorldPosition(currentWorldPosition->X, currentWorldPosition->Y, currentWorldPosition->Z, currentWorldPosition->Angle))
        {
            LastReportedWorldPosition = currentWorldPosition;
            LastWorldPositionReportTime = now;
            NextWorldPositionReportTime = now + std::chrono::milliseconds(100);
            if (PendingWorldPositionWarmupReports > 0) { --PendingWorldPositionWarmupReports; }
        }
    }

    if (!status.IsOk() && Log)
    {
        Log->Warning("D3D9 render failed: " + status.Message());
    }
}

FGameMovementInput FClientFrontendRuntime::BuildGameMovementInput(const FInputSnapshot& input, const RECT& clientRect, float& lookDeltaX, float& lookDeltaY, bool& jumpRequested)
{
    std::lock_guard<std::recursive_mutex> lock(UiMutex);
    lookDeltaX = 0.0f;
    lookDeltaY = 0.0f;
    jumpRequested = false;

    FGameMovementInput movement{};
    auto keyDown = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };

    if (Ui.Mode() != EUiRuntimeMode::Game || !input.HasFocus)
    {
        GameLookMode = false;
        LastGameTabDown = false;
        LastGameSpaceDown = false;
        LastGameMouseValid = false;
        return movement;
    }

    if (Ui.HasModalDialog() || Ui.IsGameTextInputFocused())
    {
        GameLookMode = false;
        LastGameMouseValid = false;
        return movement;
    }

    movement.Forward = keyDown('W') || keyDown(VK_UP);
    movement.Backward = keyDown('S') || keyDown(VK_DOWN);
    movement.StrafeLeft = keyDown('A') || keyDown(VK_LEFT);
    movement.StrafeRight = keyDown('D') || keyDown(VK_RIGHT);
    movement.Run = keyDown(VK_SHIFT);

    const bool spaceDown = keyDown(VK_SPACE);
    jumpRequested = spaceDown && !LastGameSpaceDown;
    LastGameSpaceDown = spaceDown;

    const bool tabDown = keyDown(VK_TAB);
    if (tabDown && !LastGameTabDown)
    {
        GameLookMode = !GameLookMode;
        LastGameMouseValid = false;
    }
    LastGameTabDown = tabDown;

    if (GameLookMode && Window.Handle())
    {
        const int centerX = (clientRect.right - clientRect.left) / 2;
        const int centerY = (clientRect.bottom - clientRect.top) / 2;
        lookDeltaX = static_cast<float>(input.MouseX - centerX);
        lookDeltaY = static_cast<float>(input.MouseY - centerY);
        if (Ui.IsGameControlChecked("control_options", 4)) { lookDeltaY = -lookDeltaY; }
        if (lookDeltaX != 0.0f || lookDeltaY != 0.0f)
        {
            POINT center{centerX, centerY};
            ClientToScreen(Window.Handle(), &center);
            SetCursorPos(center.x, center.y);
        }
        LastGameMouseX = centerX;
        LastGameMouseY = centerY;
        LastGameMouseValid = true;
        return movement;
    }

    if (Ui.ActionState().HoverWindowIndex >= 0)
    {
        LastGameMouseValid = false;
        return movement;
    }

    if (input.RightButton)
    {
        if (LastGameMouseValid)
        {
            lookDeltaX = static_cast<float>(input.MouseX - LastGameMouseX);
            lookDeltaY = static_cast<float>(input.MouseY - LastGameMouseY);
            if (Ui.IsGameControlChecked("control_options", 4)) { lookDeltaY = -lookDeltaY; }
        }
        LastGameMouseX = input.MouseX;
        LastGameMouseY = input.MouseY;
        LastGameMouseValid = true;
    }
    else
    {
        LastGameMouseValid = false;
    }

    return movement;
}

void FClientFrontendRuntime::ProcessUiAction(const std::string& action)
{
    if (action.empty()) { return; }
    AddStatusLine("ui: " + action);
    const FClientUiCommand command = UiActions.Decode(action);
    std::optional<EAudioEvent> sound = EAudioEvent::UiClick;
    switch (command.Action)
    {
    case EClientUiAction::None: sound.reset(); break;
    case EClientUiAction::Quit: sound.reset(); Shutdown(); break;
    case EClientUiAction::SaveLoginOn:
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        StoreSavedLogin(true, Ui.ActionState().LoginText, Ui.ActionState().PasswordText);
        break;
    }
    case EClientUiAction::SaveLoginOff:
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        StoreSavedLogin(false, {}, {});
        break;
    }
    case EClientUiAction::Login: BeginLoginRequest(); break;
    case EClientUiAction::Registration: sound = EAudioEvent::UiLink; AddStatusLine(Settings.RegistrationUrl.empty() ? "registration: URL absent in connectn.cfg" : "registration: " + Settings.RegistrationUrl); break;
    case EClientUiAction::CharacterSlotSelected:
    {
        SynchronizeGameState(GameState.SetSelectedCharacterSlot(command.Value));
        AddStatusLine("character: selected slot " + std::to_string(command.Value));
        break;
    }
    case EClientUiAction::CharacterEnter: BeginCharacterEnterRequest(); break;
    case EClientUiAction::CharacterCreateConfirmed: BeginCharacterCreateRequest(); break;
    case EClientUiAction::CharacterDialogChanged: sound.reset(); RepaintDirty = true; break;
    case EClientUiAction::CharacterDeleteNameRequired: AddStatusLine("character: type selected character name to confirm delete"); RepaintDirty = true; break;
    case EClientUiAction::CharacterBackRequested:
    {
        sound = EAudioEvent::UiWindowOpen;
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.ShowExitConfirmation();
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::CharacterBackConfirmed:
    {
        CloseActiveServerSession();
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.Character().SetCharacterActionLocked(false);
        Ui.Input().SetMode(EUiRuntimeMode::Login);
        Ui.SetStage("login screen ready", 1.0f);
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::CharacterDeleteRequested:
    {
        sound = EAudioEvent::UiWindowOpen;
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.ShowDeleteConfirmation();
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::CharacterDeleteConfirmed: BeginCharacterDeleteRequest(); break;
    case EClientUiAction::WindowCommand:
    {
        sound.reset();
        bool currentlyOpen = false;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            currentlyOpen = Ui.IsGameWindowVisible(command.WindowName);
        }
        const bool targetOpen = command.WindowOperation == EUiWindowOperation::Open || (command.WindowOperation == EUiWindowOperation::Toggle && !currentlyOpen);
        const FUiWindowTransition transition = targetOpen ? UiWindows.Open(command.WindowName, command.Value) : UiWindows.Close(command.WindowName);
        bool changed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            changed = Ui.SetGameWindowVisible(command.WindowName, targetOpen);
        }
        if (changed || transition.Changed)
        {
            Audio.PlayEvent(targetOpen ? EAudioEvent::UiWindowOpen : EAudioEvent::UiWindowClose);
            AddStatusLine("ui window " + command.WindowName + (transition.Open ? ": opened" : ": closed"));
            RepaintDirty = true;
        }
        break;
    }
    case EClientUiAction::WindowReplace:
    {
        sound.reset();
        bool changed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            changed = Ui.SetGameWindowVisible(command.WindowName, false) || changed;
            changed = Ui.SetGameWindowVisible(command.SecondaryWindowName, true) || changed;
        }
        const FUiWindowTransition closeTransition = UiWindows.Close(command.WindowName);
        const FUiWindowTransition openTransition = UiWindows.Open(command.SecondaryWindowName, command.Value);
        if (changed || closeTransition.Changed || openTransition.Changed)
        {
            Audio.PlayEvent(EAudioEvent::UiWindowOpen);
            AddStatusLine("ui window: " + command.WindowName + " -> " + command.SecondaryWindowName);
            RepaintDirty = true;
        }
        break;
    }
    case EClientUiAction::GameControl:
    {
        if ((Common::EqualsNoCase(command.WindowName, "statinfo") || Common::EqualsNoCase(command.WindowName, "statinfo_n")) && command.Value == 25)
        {
            std::array<int32, 8> deltas{};
            {
                std::lock_guard<std::recursive_mutex> lock(UiMutex);
                deltas = Ui.PendingStatAllocation();
            }
            if (std::any_of(deltas.begin(), deltas.end(), [](int32 value) { return value != 0; }) && NetworkComponent.SendStatAllocation(deltas))
            {
                const FGameStateChangeMask changes = GameState.CommitStatAllocation(deltas);
                {
                    std::lock_guard<std::recursive_mutex> lock(UiMutex);
                    Ui.ResetPendingStatAllocation();
                }
                SynchronizeGameState(changes);
                AddStatusLine("character: stat allocation sent");
            }
            else if (std::any_of(deltas.begin(), deltas.end(), [](int32 value) { return value != 0; })) { AddStatusLine("character: stat allocation failed"); }
            RepaintDirty = true;
            break;
        }
        if (Common::EqualsNoCase(command.WindowName, "system_right") && command.Value == 6)
        {
            std::string target;
            std::string other;
            bool opened = false;
            {
                std::lock_guard<std::recursive_mutex> lock(UiMutex);
                target = GameState.Clan().Available ? "clan" : "newclan";
                other = target == "clan" ? "newclan" : "clan";
                Ui.SetGameWindowVisible(other, false);
                opened = !Ui.IsGameWindowVisible(target);
                Ui.ToggleGameWindow(target);
            }
            UiWindows.Close(other);
            if (UiWindows.IsOpen(target)) { UiWindows.Close(target); }
            else { UiWindows.Open(target); }
            sound = opened ? EAudioEvent::UiWindowOpen : EAudioEvent::UiWindowClose;
            AddStatusLine("ui window " + target + ": toggled");
            RepaintDirty = true;
            break;
        }
        AddStatusLine("ui control: " + command.WindowName + "#" + std::to_string(command.Value));
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::GameHelp:
    {
        sound = EAudioEvent::UiWindowOpen;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetHelpTopic(command.Payload.empty() ? command.WindowName : command.Payload);
            Ui.SetGameWindowVisible("help", true);
        }
        UiWindows.Open("help");
        AddStatusLine(command.Payload.empty() ? "help: " + command.WindowName : "help: " + command.Payload);
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::GameChatSubmit:
    {
        std::string message;
        std::string sender;
        uint8 channel = 10;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            message = Ui.GameChatDraft();
            channel = Ui.GameChatChannel();
        }
        if (const FCharacterSlotInfo* character = GameState.ActiveCharacter()) { sender = Common::WideToUtf8(character->Name); }
        if (!message.empty() && !sender.empty() && NetworkComponent.SendChatMessage(channel, sender, message))
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.AppendGameChatLine(sender + ": " + message, Ui.GameChatModeIndex());
            Ui.ClearGameChatDraft();
            AddStatusLine("chat: message sent, channel=" + std::to_string(channel));
        }
        else if (!message.empty()) { AddStatusLine("chat: send failed"); }
        else { sound.reset(); }
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::GameLeaveRequested:
    {
        sound = EAudioEvent::UiWindowOpen;
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.ShowGameExitConfirmation();
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::GameLeaveConfirmed:
    {
        CloseActiveServerSession();
        UiWindows.Reset();
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.Input().SetMode(EUiRuntimeMode::Login);
        Ui.SetStage("login screen ready", 1.0f);
        RepaintDirty = true;
        break;
    }
    }
    if (sound) { Audio.PlayEvent(*sound); }
}

void FClientFrontendRuntime::BeginLoginRequest()
{
    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        login = Ui.ActionState().LoginText;
        password = Ui.ActionState().PasswordText;
    }
    const std::optional<FEndpoint> endpoint = NetworkComponent.Endpoint();
    if (!endpoint) { AddStatusLine("network: endpoint not configured"); SetStage("login failed", 1.0f); return; }
    if (login.empty() || password.empty()) { AddStatusLine("login: enter account and password"); SetStage("waiting for login", 1.0f); return; }
    if (!NetworkComponent.BeginLogin(Utf8ToWide(login), Utf8ToWide(password), AppearanceRules, static_cast<int32>(NetworkConnectTimeoutMs))) { AddStatusLine("login: request is already in progress"); return; }
    LocalCharacterName.clear();
    LocalCharacterAppearance = {};
    LocalCharacterIdentityValid = false;
    RemoteAppearanceHints.clear();
    RemotePlayerNames.clear();
    RemotePlayerEntities.clear();
    PendingAppearanceAnnouncements = 0;
    NextAppearanceAnnouncementTime = {};
    SynchronizeGameState(GameState.ResetAll());
    SetStage("connecting to " + endpoint->Host + ":" + std::to_string(endpoint->Port), 1.0f);
}

void FClientFrontendRuntime::PollLoginResult()
{
    std::optional<FLoginNetworkEvent> result = NetworkComponent.PollLogin();
    if (!result) { return; }
    AddStatusLine("network: " + result->Message);
    if (result->CharacterSelectReady)
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        const auto& state = Ui.ActionState();
        StoreSavedLogin(state.SaveLogin, state.LoginText, state.PasswordText);
        Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
        Ui.SetStage("character select ready", 1.0f);
    }
    else if (result->Connected) { SetStage("server answered", 1.0f); }
    else { SetStage("login failed", 1.0f); }
    if (Log) { Log->Info("login probe result: " + result->Message); }
    RepaintDirty = true;
}
void FClientFrontendRuntime::BeginCharacterEnterRequest()
{
    const int32 slot = GameState.Characters().SelectedSlot;
    const FCharacterSlotInfo* selected = GameState.SelectedCharacter();
    const bool present = selected && selected->Present;
    const bool canCreate = selected && selected->CanCreate;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.Character().SetCharacterActionLocked(true);
    }
    auto unlock = [this]() { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.Character().SetCharacterActionLocked(false); };
    if (!NetworkComponent.HasActiveSession()) { unlock(); AddStatusLine("character: server session is not active"); return; }
    if (!present) { unlock(); AddStatusLine(canCreate ? "character: creation requires confirmation" : "character: selected slot is unavailable"); return; }
    if (!NetworkComponent.BeginCharacterEnter(slot, static_cast<int32>(NetworkConnectTimeoutMs))) { unlock(); AddStatusLine("character: another action is already in progress"); return; }
    LocalCharacterName = selected->Name;
    LocalCharacterAppearance = FCharacterCreationAppearance{selected->Female, AppearanceRules.ModelBase, selected->Face, selected->Hair, selected->HairColor, selected->Tattoo};
    LocalCharacterIdentityValid = true;
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.SetInitialGameWorldPosition(std::nullopt);
        RenderDevice.ClearRemoteGamePlayers();
        RenderDevice.ClearRemoteGameActors();
    }
    LastReportedWorldPosition.reset();
    LastWorldPositionReportTime = {};
    NextWorldPositionReportTime = {};
    PendingWorldPositionWarmupReports = 2;
    RemoteAppearanceHints.clear();
    RemotePlayerNames.clear();
    RemotePlayerEntities.clear();
    NextAppearanceAnnouncementTime = {};
    PendingAppearanceAnnouncements = 0;
    GameState.SetSelectedCharacterSlot(slot);
    SynchronizeGameState(GameState.ResetWorld());
    SetStage("entering world", 1.0f);
}

void FClientFrontendRuntime::BeginCharacterCreateRequest()
{
    const int32 slot = GameState.Characters().SelectedSlot;
    const FCharacterSlotInfo* selected = GameState.SelectedCharacter();
    const bool present = selected && selected->Present;
    const bool canCreate = selected && selected->CanCreate;
    std::wstring name;
    FCharacterCreationAppearance appearance;
    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        name = Ui.Character().SelectedCharacterName();
        appearance = Ui.Character().SelectedCharacterAppearance(AppearanceRules);
        login = Ui.ActionState().LoginText;
        password = Ui.ActionState().PasswordText;
        Ui.Character().SetCharacterActionLocked(true);
    }
    auto unlock = [this]() { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.Character().SetCharacterActionLocked(false); };
    if (!NetworkComponent.HasActiveSession()) { unlock(); AddStatusLine("character: server session is not active"); return; }
    if (present || !canCreate) { unlock(); AddStatusLine("character: selected slot is not creatable"); return; }
    if (name.empty()) { unlock(); AddStatusLine("character: name is empty"); return; }
    if (!NetworkComponent.BeginCharacterCreate(slot, std::move(name), appearance, Utf8ToWide(login), Utf8ToWide(password), static_cast<int32>(NetworkConnectTimeoutMs))) { unlock(); AddStatusLine("character: another action is already in progress"); return; }
    SetStage("creating character", 1.0f);
}

void FClientFrontendRuntime::BeginCharacterDeleteRequest()
{
    const int32 slot = GameState.Characters().SelectedSlot;
    const FCharacterSlotInfo* selected = GameState.SelectedCharacter();
    const bool present = selected && selected->Present;
    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        login = Ui.ActionState().LoginText;
        password = Ui.ActionState().PasswordText;
        Ui.Character().SetCharacterActionLocked(true);
    }
    auto unlock = [this]() { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.Character().SetCharacterActionLocked(false); };
    if (!NetworkComponent.HasActiveSession()) { unlock(); AddStatusLine("character: server session is not active"); return; }
    if (!present) { unlock(); AddStatusLine("character: selected slot is empty"); return; }
    if (!NetworkComponent.BeginCharacterDelete(slot, Utf8ToWide(login), Utf8ToWide(password), static_cast<int32>(NetworkConnectTimeoutMs))) { unlock(); AddStatusLine("character: another action is already in progress"); return; }
    SetStage("deleting character", 1.0f);
}

void FClientFrontendRuntime::PollCharacterResult()
{
    std::optional<FCharacterNetworkEvent> event = NetworkComponent.PollCharacter();
    if (!event) { return; }
    AddStatusLine("character: " + event->Message);
    const bool mutation = event->Action != ECharacterNetworkAction::Enter;
    if (event->Ok && mutation)
    {
        if (event->RefreshReady)
        {
            AddStatusLine("character: refreshed slots after mutation; " + event->RefreshMessage);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
            Ui.SetStage("character select ready", 1.0f);
        }
        else
        {
            const std::string message = event->RefreshAttempted ? event->RefreshMessage : "refresh was not started";
            AddStatusLine("character: mutation completed, but charlist refresh failed: " + message);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::Login);
            Ui.SetStage("login screen ready", 1.0f);
        }
    }
    else if (event->Ok)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::Game);
            Ui.SetStage("game session active", 1.0f);
        }
        NetworkComponent.StartWorldEventPump();
        QueueAppearanceAnnouncement(3);
        UpdateWorldMusic();
    }
    else
    {
        if (event->Action == ECharacterNetworkAction::Enter)
        {
            LocalCharacterName.clear();
            LocalCharacterAppearance = {};
            LocalCharacterIdentityValid = false;
            PendingAppearanceAnnouncements = 0;
            NextAppearanceAnnouncementTime = {};
        }
        const char* stage = event->Action == ECharacterNetworkAction::Create ? "character create failed" : event->Action == ECharacterNetworkAction::Delete ? "character delete failed" : "character enter failed";
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.Character().SetCharacterActionLocked(false);
        Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
        Ui.SetStage(stage, 1.0f);
    }
    RepaintDirty = true;
}

void FClientFrontendRuntime::QueueAppearanceAnnouncement(int32 count)
{
    PendingAppearanceAnnouncements = (std::max)(PendingAppearanceAnnouncements, (std::max)(0, count));
    if (PendingAppearanceAnnouncements > 0) { NextAppearanceAnnouncementTime = {}; }
}

void FClientFrontendRuntime::TrySendAppearanceAnnouncement()
{
    if (PendingAppearanceAnnouncements <= 0 || !NetworkComponent.HasActiveSession()) { return; }
    const auto now = std::chrono::steady_clock::now();
    if (NextAppearanceAnnouncementTime.time_since_epoch().count() != 0 && now < NextAppearanceAnnouncementTime) { return; }
    if (!LocalCharacterIdentityValid || LocalCharacterName.empty() || GameState.Session().LocalEntityId == 0) { return; }
    const std::string sender = BuildAppearanceSender(LocalCharacterName, LocalCharacterAppearance);
    if (sender.empty())
    {
        PendingAppearanceAnnouncements = 0;
        if (Log) { Log->Warning("appearance sync skipped: character name cannot be encoded"); }
        return;
    }
    if (NetworkComponent.SendChatMessage(3, sender, "."))
    {
        --PendingAppearanceAnnouncements;
        if (Log) { Log->Info("appearance sync sent: " + Common::WideToUtf8(LocalCharacterName)); }
        NextAppearanceAnnouncementTime = PendingAppearanceAnnouncements > 0 ? now + std::chrono::milliseconds(700) : std::chrono::steady_clock::time_point{};
    }
    else { NextAppearanceAnnouncementTime = now + std::chrono::seconds(1); }
}

void FClientFrontendRuntime::ProcessServerEvents()
{
    std::vector<FServerEvent> events = NetworkComponent.DrainServerEvents();
    if (events.empty()) { return; }
    bool sessionClosed = false;
    bool sawNewRemotePlayer = false;
    const std::string localPlayerName = LocalCharacterIdentityValid ? Common::WideToUtf8(LocalCharacterName) : std::string{};
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        for (const FServerEvent& event : events)
        {
            if (const auto* appearance = std::get_if<FRemotePlayerAppearanceEvent>(&event.Payload))
            {
                if (!localPlayerName.empty() && appearance->Name == localPlayerName) { continue; }
                const auto previous = RemoteAppearanceHints.find(appearance->Name);
                const bool changed = previous == RemoteAppearanceHints.end() || !SameAppearance(previous->second, appearance->Appearance);
                RemoteAppearanceHints[appearance->Name] = appearance->Appearance;
                const auto known = RemotePlayerEntities.find(appearance->Name);
                if (known != RemotePlayerEntities.end()) { RenderDevice.SetRemoteGamePlayerAppearance(known->second, appearance->Appearance); }
                if (changed && Log) { Log->Info("appearance sync received: " + appearance->Name); }
                continue;
            }
            if (const auto* spawn = std::get_if<FRemotePlayerSpawnEvent>(&event.Payload))
            {
                if (!localPlayerName.empty() && spawn->Name == localPlayerName) { continue; }
                const auto known = RemotePlayerNames.find(spawn->EntityId);
                sawNewRemotePlayer = sawNewRemotePlayer || known == RemotePlayerNames.end() || known->second != spawn->Name;
                if (known != RemotePlayerNames.end() && known->second != spawn->Name)
                {
                    const auto oldEntity = RemotePlayerEntities.find(known->second);
                    if (oldEntity != RemotePlayerEntities.end() && oldEntity->second == spawn->EntityId) { RemotePlayerEntities.erase(oldEntity); }
                }
                RemotePlayerNames[spawn->EntityId] = spawn->Name;
                RemotePlayerEntities[spawn->Name] = spawn->EntityId;
                const FCharacterCreationAppearance* resolvedAppearance = &spawn->Appearance;
                const auto hint = RemoteAppearanceHints.find(spawn->Name);
                if (hint != RemoteAppearanceHints.end()) { resolvedAppearance = &hint->second; }
                RenderDevice.UpsertRemoteGamePlayer(MakeRemotePlayer(spawn->EntityId, spawn->Name, spawn->Position, resolvedAppearance));
            }
            else if (const auto* actorSpawn = std::get_if<FRemoteActorSpawnEvent>(&event.Payload))
            {
                if (!SferaProtocol::IsMonsterObjectType(actorSpawn->ObjectType)) { continue; }
                const auto known = RemotePlayerNames.find(actorSpawn->EntityId);
                if (known != RemotePlayerNames.end())
                {
                    const auto entity = RemotePlayerEntities.find(known->second);
                    if (entity != RemotePlayerEntities.end() && entity->second == actorSpawn->EntityId) { RemotePlayerEntities.erase(entity); }
                    RemotePlayerNames.erase(known);
                }
                RenderDevice.UpsertRemoteGameActor(MakeRemoteActor(*actorSpawn));
            }
            else if (const auto* move = std::get_if<FRemotePlayerMoveEvent>(&event.Payload)) { RenderDevice.UpdateRemoteGameEntityPosition(move->EntityId, FGameWorldPosition{move->Position.X, move->Position.Y, move->Position.Z, move->Position.Angle}); }
            else if (const auto* despawn = std::get_if<FRemotePlayerDespawnEvent>(&event.Payload))
            {
                const auto known = RemotePlayerNames.find(despawn->EntityId);
                if (known != RemotePlayerNames.end())
                {
                    const auto entity = RemotePlayerEntities.find(known->second);
                    if (entity != RemotePlayerEntities.end() && entity->second == despawn->EntityId) { RemotePlayerEntities.erase(entity); }
                    RemotePlayerNames.erase(known);
                }
                RenderDevice.RemoveRemoteGameEntity(despawn->EntityId);
            }
            else if (std::holds_alternative<FSessionClosedEvent>(event.Payload)) { sessionClosed = true; }
        }
        if (sessionClosed) { RenderDevice.ClearRemoteGamePlayers(); RenderDevice.ClearRemoteGameActors(); }
    }
    if (sawNewRemotePlayer) { QueueAppearanceAnnouncement(3); }
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        for (const FServerEvent& event : events)
        {
            const auto* chat = std::get_if<FChatMessageEvent>(&event.Payload);
            if (!chat) { continue; }
            if (!localPlayerName.empty() && chat->Sender == localPlayerName) { continue; }
            const int32 mode = std::clamp(static_cast<int32>(chat->Channel) - 1, 0, 4);
            Ui.AppendGameChatLine((chat->Sender.empty() ? std::string("?") : chat->Sender) + ": " + chat->Text, mode);
        }
    }
    const FGameStateChangeMask changes = GameState.Apply(events);
    const FGameStateChangeMask visibleChanges = changes & ~GameStateChange::Diagnostics;
    if (visibleChanges != GameStateChange::None) { SynchronizeGameState(visibleChanges); }
    const std::string& closeReason = GameState.Session().LastDisconnectReason;
    if ((changes & GameStateChange::Session) != 0 && !GameState.Session().Connected && !closeReason.empty())
    {
        LastReportedWorldPosition.reset();
        LastWorldPositionReportTime = {};
        NextWorldPositionReportTime = {};
        PendingWorldPositionWarmupReports = 0;
        LocalCharacterName.clear();
        LocalCharacterAppearance = {};
        LocalCharacterIdentityValid = false;
        RemoteAppearanceHints.clear();
        RemotePlayerNames.clear();
        RemotePlayerEntities.clear();
        NextAppearanceAnnouncementTime = {};
        PendingAppearanceAnnouncements = 0;
        UiWindows.Reset();
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::Login);
            Ui.SetStage("login screen ready", 1.0f);
        }
        AddStatusLine("network: " + closeReason);
        RepaintDirty = true;
    }
}


void FClientFrontendRuntime::ApplyAudioVolumes(float soundVolume, float musicVolume)
{
    soundVolume = std::clamp(soundVolume, 0.0f, 1.0f);
    musicVolume = std::clamp(musicVolume, 0.0f, 1.0f);
    const bool alreadyInitialized = AppliedSoundVolume >= 0.0f && AppliedMusicVolume >= 0.0f;
    bool changed = false;
    if (std::abs(soundVolume - AppliedSoundVolume) > 0.0001f)
    {
        Audio.SetBusVolume(EAudioBus::Ui, soundVolume);
        Audio.SetBusVolume(EAudioBus::Effects, soundVolume);
        Audio.SetBusVolume(EAudioBus::Creatures, soundVolume);
        Audio.SetBusVolume(EAudioBus::Ambience, soundVolume);
        AppliedSoundVolume = soundVolume;
        Settings.SoundVolume = soundVolume;
        changed = true;
    }
    if (std::abs(musicVolume - AppliedMusicVolume) > 0.0001f)
    {
        Audio.SetBusVolume(EAudioBus::Music, musicVolume);
        AppliedMusicVolume = musicVolume;
        Settings.MusicVolume = musicVolume;
        changed = true;
    }
    if (changed && alreadyInitialized)
    {
        AudioSettingsDirty = true;
        LastAudioSettingsChange = std::chrono::steady_clock::now();
    }
}

void FClientFrontendRuntime::FlushAudioSettings(bool force)
{
    if (!AudioSettingsDirty) { return; }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - LastAudioSettingsChange < std::chrono::milliseconds(350)) { return; }
    const FStatus status = SaveClientAudioSettings(Settings.SoundVolume, Settings.MusicVolume);
    AudioSettingsDirty = false;
    if (!Log) { return; }
    if (status.IsOk()) { Log->Info("audio settings saved: sound=" + std::to_string(static_cast<int32>(std::lround(Settings.SoundVolume * 100.0f))) + ", music=" + std::to_string(static_cast<int32>(std::lround(Settings.MusicVolume * 100.0f)))); }
    else { Log->Warning("audio settings save failed: " + status.Message()); }
}

void FClientFrontendRuntime::SetMusicCue(std::string cue, float fadeSeconds)
{
    cue = Common::ToLower(std::move(cue));
    if (!Audio.IsInitialized()) { ActiveMusicCue.clear(); return; }
    if (cue.empty() || cue == ActiveMusicCue) { return; }
    Audio.PlayMusic(FMusicRequest{cue, 1.0f, std::max(0.0f, fadeSeconds)});
    ActiveMusicCue = std::move(cue);
    if (Log) { Log->Info("music cue requested: " + ActiveMusicCue); }
}

std::string FClientFrontendRuntime::SelectWorldMusicCue() const
{
    const FGameWorldState& world = GameState.World();
    if (!world.HasPosition || !RenderWorldScene) { return "mounts_1d"; }
    double positionX = world.Position.X;
    double positionZ = world.Position.Z;
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        if (const auto currentPosition = RenderDevice.CurrentGameWorldPosition())
        {
            positionX = currentPosition->X;
            positionZ = currentPosition->Z;
        }
    }
    const int32 worldX = static_cast<int32>(positionX);
    const int32 worldZ = static_cast<int32>(positionZ);
    const int32 mapX = (worldX + 4000) / 100;
    const int32 mapZ = (3999 - worldZ) / 100;
    const FWorldMapCell* cell = RenderWorldScene->FindMapRegionCell(mapX, mapZ);
    if (!cell) { return "mounts_1d"; }
    const std::string tile = Common::ToLower(cell->TileName);
    const auto starts = [&tile](std::string_view prefix) { return tile.starts_with(prefix); };
    const FWorldContourRecord* markerContour = RenderWorldScene->FindContourAt(static_cast<float>(worldX), static_cast<float>(worldZ), 4000, 4099);
    const FWorldContourRecord* settlementContour = RenderWorldScene->FindContourAt(static_cast<float>(worldX), static_cast<float>(worldZ), 3001, 3002);
    int32 marker = markerContour ? markerContour->SortKey : 0;
    int32 region = 1;
    std::string regionSource = markerContour ? "contour" : "tile";
    if (marker > 0)
    {
        switch (marker)
        {
        case 4001: region = 9; break;
        case 4002: region = 2; break;
        case 4010: region = 10; break;
        case 4011: region = 11; break;
        case 4012: region = 12; break;
        case 4004: region = 13; break;
        default: region = 9; break;
        }
    }
    else if (starts("town_ph00")) { region = 14; }
    else if (starts("cemetry")) { region = 8; }
    else if (starts("forest") || starts("patch2")) { region = 7; }
    else if (starts("castle") || starts("cc_")) { region = 3; }
    else if (starts("river") || starts("lake")) { region = 4; }
    else if (starts("cliff")) { region = 5; }
    else if (starts("mount")) { region = 6; }
    else if (starts("town")) { region = 2; }
    FWorldMusicRegionEvidence staticEvidence;
    if (marker == 0 && region == 1)
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        staticEvidence = RenderDevice.QueryMusicRegionEvidence(static_cast<float>(worldX), static_cast<float>(worldZ), 60.0f);
        if (!staticEvidence.Available) { return ActiveMusicCue.empty() ? "mounts_1d" : ActiveMusicCue; }
        const bool settlementConfirmed = settlementContour && staticEvidence.CityScore >= 10 && staticEvidence.CityAnchorCount >= 2;
        const bool denseCityCluster = staticEvidence.CityScore >= 25 && staticEvidence.CityAnchorCount >= 5;
        if (settlementConfirmed || denseCityCluster)
        {
            marker = 4002;
            region = 2;
            regionSource = settlementConfirmed ? "settlement_contour+static_city_evidence" : "static_city_evidence";
        }
    }
    const FGameClockState& clock = GameState.Clock();
    const float hour = clock.Known ? clock.DayFraction * 24.0f : 12.0f;
    const bool day = hour >= 7.0f && hour < 19.0f;
    std::string cue;
    std::string_view regionName = "desert";
    switch (region)
    {
    case 2: regionName = "city"; cue = day ? "city_1d" : "city_1n"; break;
    case 3:
    case 10: regionName = "castle"; cue = day ? "castle_1" : "castle_2"; break;
    case 4:
    case 13: regionName = "water"; cue = day ? "water_1" : "water_2"; break;
    case 5: regionName = "cliff"; cue = "mounts_1n"; break;
    case 6:
    case 11: regionName = "mountains"; cue = day ? "mounts_1d" : "mounts_1n"; break;
    case 7: regionName = "forest"; cue = day ? "forest_1d" : "forest_1n"; break;
    case 8:
    case 12: regionName = "graveyard"; cue = day ? "grave_1d" : "grave_1n"; break;
    case 9: regionName = "special_desert"; cue = day ? "desert_2d" : "desert_2n"; break;
    case 14: regionName = "special_city"; cue = day ? "city_2f" : "mounts_1d"; break;
    default: cue = day ? "desert_1d" : "desert_1n"; break;
    }
    if (Log && cue != ActiveMusicCue)
    {
        const int32 spanX = std::max(1, static_cast<int32>(cell->Reserved & 0xff));
        const int32 spanZ = std::max(1, static_cast<int32>((cell->Reserved >> 8) & 0xff));
        std::string context = "world music context: world_x=" + std::to_string(worldX) + ", world_z=" + std::to_string(worldZ) + ", map_x=" + std::to_string(mapX) + ", map_z=" + std::to_string(mapZ) + ", tile=" + tile + ", tile_anchor=" + std::to_string(cell->X) + "," + std::to_string(cell->Z) + ", expanded=" + ((cell->X != mapX || cell->Z != mapZ) ? "yes" : "no") + ", tile_span=" + std::to_string(spanX) + "x" + std::to_string(spanZ) + ", region_source=" + regionSource + ", marker=" + std::to_string(marker);
        if (markerContour) { context += ", contour_index=" + std::to_string(markerContour->Index); }
        if (settlementContour) { context += ", settlement_marker=" + std::to_string(settlementContour->SortKey) + ", settlement_contour_index=" + std::to_string(settlementContour->Index); }
        if (staticEvidence.Available)
        {
            context += ", city_score=" + std::to_string(staticEvidence.CityScore) + ", city_anchors=" + std::to_string(staticEvidence.CityAnchorCount);
            if (!staticEvidence.NearestCityAnchor.empty()) { context += ", nearest_city_anchor=" + staticEvidence.NearestCityAnchor + ", nearest_city_distance=" + std::to_string(static_cast<int32>(std::lround(staticEvidence.NearestCityAnchorDistance))); }
        }
        context += ", region=" + std::string(regionName) + ", period=" + (day ? "day" : "night") + ", cue=" + cue;
        Log->Info(context);
    }
    return cue;
}

void FClientFrontendRuntime::UpdateWorldMusic()
{
    if (!GameState.Session().Connected || !GameState.Session().InWorld) { SetMusicCue("mounts_1d"); return; }
    const std::string selectedCue = SelectWorldMusicCue();
    if (Log && selectedCue != ActiveMusicCue) { Log->Info("world music selected by SelectWorldMusicCue: cue=" + selectedCue); }
    SetMusicCue(selectedCue);
}

void FClientFrontendRuntime::SynchronizeGameState(FGameStateChangeMask changes)
{
    if (changes == GameStateChange::None) { return; }
    const FGameCharacterState& characters = GameState.Characters();
    const FGameWorldState& world = GameState.World();
    const FGameClockState& clock = GameState.Clock();
    const FGameMapState& map = GameState.Map();
    const FGameClanState& clan = GameState.Clan();

    if ((changes & (GameStateChange::Session | GameStateChange::WorldPosition | GameStateChange::Clock | GameStateChange::Map)) != 0) { UpdateWorldMusic(); }

    if ((changes & GameStateChange::Clock) != 0)
    {
        Audio.SetGameTimeHours(clock.Known ? clock.DayFraction * 24.0f : -1.0f);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        if (clock.Known) { RenderDevice.SetServerGameTime(clock.DayFraction); }
        else { RenderDevice.ClearServerGameTime(); }
    }

    if ((changes & GameStateChange::WorldPosition) != 0)
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        if (world.HasPosition)
        {
            FGameWorldPosition position;
            position.X = world.Position.X;
            position.Y = world.Position.Y;
            position.Z = world.Position.Z;
            position.Angle = world.Position.Angle;
            if (AppliedWorldRevision == 0) { RenderDevice.SetInitialGameWorldPosition(position); }
            else { RenderDevice.ApplyServerGameWorldPosition(position); }
        }
        else { RenderDevice.SetInitialGameWorldPosition(std::nullopt); }
        AppliedWorldRevision = world.Revision;
    }

    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        if ((changes & GameStateChange::CharacterRoster) != 0 && AppliedRosterRevision != characters.RosterRevision)
        {
            Ui.Character().SetCharacterSlots(characters.Slots);
            AppliedRosterRevision = characters.RosterRevision;
        }
        if ((changes & (GameStateChange::CharacterRoster | GameStateChange::ActiveCharacter)) != 0) { Ui.Character().SetSelectedSlot(characters.SelectedSlot); }
        if ((changes & GameStateChange::Clock) != 0)
        {
            if (clock.Known) { Ui.SetServerGameTime(clock.DayFraction, clock.Day, clock.Month, clock.Year); }
            else { Ui.ClearServerGameTime(); }
        }
        if ((changes & GameStateChange::Map) != 0 || (changes & GameStateChange::Session) != 0)
        {
            Ui.SetMapDescriptor(map.SpriteName, map.Projection, map.HasProjection);
        }
        if ((changes & GameStateChange::Clan) != 0 || (changes & GameStateChange::Session) != 0)
        {
            Ui.SetClanAvailable(clan.Available);
        }
        if ((changes & GameStateChange::WorldPosition) != 0)
        {
            if (world.HasPosition) { Ui.SetMapPlayerPosition(world.Position.X, world.Position.Z); }
            else { Ui.ClearMapPlayerPosition(); }
        }
    }
    RepaintDirty = true;
}
FStatus FClientFrontendRuntime::RunEventLoop()
{
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend event loop requested before window shell creation"); }

    if (Log)
    {
        Log->Info("frontend event loop started");
    }

    LastPaint = std::chrono::steady_clock::now();
    auto LastFrameStart = std::chrono::steady_clock::now();
    auto lastAudioUpdate = LastFrameStart;

    while (Window.IsOpen() && Window.PumpMessages())
    {
        const auto audioUpdateTime = std::chrono::steady_clock::now();
        Audio.Update(ClampFrameDelta(SecondsBetween(lastAudioUpdate, audioUpdateTime)));
        lastAudioUpdate = audioUpdateTime;
        FInputSnapshot input = Window.ConsumeInputFrame();
        RECT client{};

        if (Window.Handle())
        {
            GetClientRect(Window.Handle(), &client);
        }

        bool inputChanged = false;
        std::string action;
        std::pair<float, float> audioVolumes;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            inputChanged = Ui.Input().HandleInputFrame(input, client, Log);
            action = Ui.ConsumeLastAction();
            audioVolumes = Ui.AudioOptionVolumes();
        }
        ApplyAudioVolumes(audioVolumes.first, audioVolumes.second);
        FlushAudioSettings(false);

        if (!action.empty())
        {
            ProcessUiAction(action);
            inputChanged = true;
        }

        if (inputChanged)
        {
            RepaintDirty = true;
        }

        PollLoginResult();
        PollCharacterResult();
        ProcessServerEvents();
        bool gameMode = false;
        bool characterSelectMode = false;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            const EUiRuntimeMode mode = Ui.Mode();
            gameMode = mode == EUiRuntimeMode::Game;
            characterSelectMode = mode == EUiRuntimeMode::CharacterSelect;
        }

        bool uiTexturePreloading = false;
        if (D3DInitialized.load())
        {
            std::lock_guard<std::mutex> renderLock(RenderMutex);
            uiTexturePreloading = RenderDevice.HasPendingUiTexturePreload();
        }
        const bool animatedMode = gameMode || characterSelectMode || uiTexturePreloading;
        const bool repaintDue = RepaintDirty && !animatedMode;
        const bool shouldRender = D3DInitialized.load() && (animatedMode || repaintDue);

        if (shouldRender)
        {
            const auto frameStart = std::chrono::steady_clock::now();
            float lookDeltaX = 0.0f;
            float lookDeltaY = 0.0f;
            bool jumpRequested = false;
            FGameMovementInput gameInput = BuildGameMovementInput(input, client, lookDeltaX, lookDeltaY, jumpRequested);
            const float deltaSeconds = ClampFrameDelta(SecondsBetween(LastFrameStart, frameStart));
            LastFrameStart = frameStart;
            RenderFrame(deltaSeconds, gameInput, lookDeltaX, lookDeltaY, jumpRequested);
            RepaintDirty = false;
        }
        else if (RepaintDirty && !animatedMode)
        {
            RequestRepaintThrottled();
            Sleep(1);
        }
        else
        {
            MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    if (Log)
    {
        Log->Info("frontend event loop finished");
    }

    FlushAudioSettings(true);

    return FStatus::Ok();
}

void FClientFrontendRuntime::Shutdown()
{
    FlushAudioSettings(true);
    NetworkComponent.Shutdown();
    Audio.Shutdown();
    ActiveMusicCue.clear();
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.Shutdown();
    }
    Window.Destroy();
    ShellCreated = false;
    D3DInitialized.store(false);
    RenderResources = nullptr;
    RenderWorldScene = nullptr;
}

void FClientFrontendRuntime::RequestRepaintThrottled()
{
    if (!ShellCreated || D3DInitialized.load()) { return; }

    auto now = std::chrono::steady_clock::now();

    if (RepaintDirty || now - LastPaint > std::chrono::milliseconds(50))
    {
        Window.RequestRepaint();
        LastPaint = now;
    }
}


