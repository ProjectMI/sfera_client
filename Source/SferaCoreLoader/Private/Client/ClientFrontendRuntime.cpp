#include "Client/ClientFrontendRuntime.h"
#include "Common/SferaGameConstants.h"
#include "Common/TextEncoding.h"
#include "Common/StringUtils.h"

FClientFrontendRuntime::FClientFrontendRuntime(FLogger* Logger) : Log(Logger), NetworkComponent(Logger) {}
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
    ClanRuntime.Reset();
    MapRuntime.Reset();
    LastAppliedServerWorldPosition.reset();
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

void FClientFrontendRuntime::PollServerWorldUpdates()
{
    if (!NetworkComponent.HasActiveSession()) { return; }
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        if (Ui.Mode() != EUiRuntimeMode::Game) { return; }
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - LastServerWorldPoll).count() < 50) { return; }
    LastServerWorldPoll = now;
    FCharacterActionResult world = NetworkComponent.PollWorldFrames(32);
    const bool clanChanged = ClanRuntime.InspectFrames(world.Frames);
    if (MapRuntime.InspectFrames(world.Frames))
    {
        const FMapRuntimeState& map = MapRuntime.State();
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetMapDescriptor(map.SpriteName, map.Projection, map.HasProjection);
        RepaintDirty = true;
    }
    if (clanChanged)
    {
        const FClanRuntimeState& clan = ClanRuntime.State();
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetClanAvailable(clan.Available);
        RepaintDirty = true;
    }
    if (!world.ServerPosition) { return; }

    FGameWorldPosition position;
    position.X = world.ServerPosition->X;
    position.Y = world.ServerPosition->Y;
    position.Z = world.ServerPosition->Z;
    position.Angle = world.ServerPosition->Angle;
    if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z) || std::abs(position.X) > 20000.0 || std::abs(position.Y) > 20000.0 || std::abs(position.Z) > 20000.0)
    {
        if (Log) { Log->Warning("server world position rejected: out of sane bounds"); }
        return;
    }

    const bool meaningfulUpdate = !LastAppliedServerWorldPosition || std::abs(LastAppliedServerWorldPosition->X - position.X) > 0.05 || std::abs(LastAppliedServerWorldPosition->Y - position.Y) > 0.05 || std::abs(LastAppliedServerWorldPosition->Z - position.Z) > 0.05 || std::abs(LastAppliedServerWorldPosition->Angle - position.Angle) > 0.01;
    if (!meaningfulUpdate) { return; }
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.ApplyServerGameWorldPosition(position);
    }
    LastAppliedServerWorldPosition = position;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetMapPlayerPosition(position.X, position.Z);
    }
}

void FClientFrontendRuntime::RenderFrame(float deltaSeconds, FGameMovementInput gameInput, float lookDeltaX, float lookDeltaY, bool jumpRequested)
{
    if (!ShellCreated || !Window.Handle()) { return; }

    if (!D3DInitialized.load() || !RenderResources) { return; }

    PollServerWorldUpdates();

    RECT client{};
    GetClientRect(Window.Handle(), &client);
    FStatus status;
    {
        std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        Ui.SetCompassHeading(RenderDevice.GameWorldCameraFacing());
        Ui.Tick(deltaSeconds);
        status = RenderDevice.RenderUiDesktop(*RenderResources, RenderWorldScene, Ui, client, deltaSeconds, gameInput, lookDeltaX, lookDeltaY, jumpRequested, Log);
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
    switch (command.Action)
    {
    case EClientUiAction::Quit: Shutdown(); break;
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
    case EClientUiAction::Registration: AddStatusLine(Settings.RegistrationUrl.empty() ? "registration: URL absent in connectn.cfg" : "registration: " + Settings.RegistrationUrl); break;
    case EClientUiAction::CharacterSlotSelected: AddStatusLine("character: selected slot " + std::to_string(command.Value)); break;
    case EClientUiAction::CharacterEnter: BeginCharacterEnterRequest(); break;
    case EClientUiAction::CharacterCreateConfirmed: BeginCharacterCreateRequest(); break;
    case EClientUiAction::CharacterDialogChanged: RepaintDirty = true; break;
    case EClientUiAction::CharacterDeleteNameRequired: AddStatusLine("character: type selected character name to confirm delete"); RepaintDirty = true; break;
    case EClientUiAction::CharacterBackRequested:
    {
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
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.ShowDeleteConfirmation();
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::CharacterDeleteConfirmed: BeginCharacterDeleteRequest(); break;
    case EClientUiAction::WindowCommand:
    {
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
        if (changed || transition.Changed) { AddStatusLine("ui window " + command.WindowName + (transition.Open ? ": opened" : ": closed")); RepaintDirty = true; }
        break;
    }
    case EClientUiAction::WindowReplace:
    {
        bool changed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            changed = Ui.SetGameWindowVisible(command.WindowName, false) || changed;
            changed = Ui.SetGameWindowVisible(command.SecondaryWindowName, true) || changed;
        }
        const FUiWindowTransition closeTransition = UiWindows.Close(command.WindowName);
        const FUiWindowTransition openTransition = UiWindows.Open(command.SecondaryWindowName, command.Value);
        if (changed || closeTransition.Changed || openTransition.Changed) { AddStatusLine("ui window: " + command.WindowName + " -> " + command.SecondaryWindowName); RepaintDirty = true; }
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
                std::lock_guard<std::recursive_mutex> lock(UiMutex);
                Ui.CommitPendingStatAllocation();
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
            {
                std::lock_guard<std::recursive_mutex> lock(UiMutex);
                target = Ui.IsClanAvailable() ? "clan" : "newclan";
                other = target == "clan" ? "newclan" : "clan";
                Ui.SetGameWindowVisible(other, false);
                Ui.ToggleGameWindow(target);
            }
            UiWindows.Close(other);
            if (UiWindows.IsOpen(target)) { UiWindows.Close(target); }
            else { UiWindows.Open(target); }
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
            sender = Common::WideToUtf8(Ui.Character().SelectedCharacterName());
            channel = Ui.GameChatChannel();
        }
        if (!message.empty() && NetworkComponent.SendChatMessage(channel, message))
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.AppendGameChatLine((sender.empty() ? std::string("Вы") : sender) + ": " + message, Ui.GameChatModeIndex());
            Ui.ClearGameChatDraft();
            AddStatusLine("chat: message sent, channel=" + std::to_string(channel));
        }
        else if (!message.empty()) { AddStatusLine("chat: send failed"); }
        RepaintDirty = true;
        break;
    }
    case EClientUiAction::GameLeaveRequested:
    {
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
    default: break;
    }
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
    SetStage("connecting to " + endpoint->Host + ":" + std::to_string(endpoint->Port), 1.0f);
}

void FClientFrontendRuntime::PollLoginResult()
{
    std::optional<FLoginProbeResult> result = NetworkComponent.PollLogin();
    if (!result) { return; }
    AddStatusLine("network: " + result->Message);
    if (result->CharacterSelectReady)
    {
        if (result->HasGameTime)
        {
            {
                std::lock_guard<std::mutex> renderLock(RenderMutex);
                RenderDevice.SetServerGameTime(result->GameTimeFraction);
            }
            std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
            Ui.SetServerGameTime(result->GameTimeFraction, result->GameDay, result->GameMonth, result->GameYear);
        }
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            const auto& state = Ui.ActionState();
            StoreSavedLogin(state.SaveLogin, state.LoginText, state.PasswordText);
            Ui.Character().SetCharacterSlots(result->CharacterSlots);
            Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
            Ui.SetStage("character select ready", 1.0f);
        }
    }
    else if (result->Connected) { SetStage("server answered", 1.0f); }
    else { SetStage("login failed", 1.0f); }
    if (Log) { Log->Info("login probe result: " + result->Message); }
    RepaintDirty = true;
}

void FClientFrontendRuntime::BeginCharacterEnterRequest()
{
    int32 slot = 0;
    bool present = false;
    bool canCreate = false;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.Character().SelectedSlotIndex();
        present = Ui.Character().SelectedCharacterPresent();
        canCreate = Ui.Character().SelectedCharacterCanCreate();
        Ui.Character().SetCharacterActionLocked(true);
    }
    auto unlock = [this]() { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.Character().SetCharacterActionLocked(false); };
    if (!NetworkComponent.HasActiveSession()) { unlock(); AddStatusLine("character: server session is not active"); return; }
    if (!present) { unlock(); AddStatusLine(canCreate ? "character: creation requires confirmation" : "character: selected slot is unavailable"); return; }
    if (!NetworkComponent.BeginCharacterEnter(slot, static_cast<int32>(NetworkConnectTimeoutMs))) { unlock(); AddStatusLine("character: another action is already in progress"); return; }
    {
        std::lock_guard<std::mutex> renderLock(RenderMutex);
        RenderDevice.SetInitialGameWorldPosition(std::nullopt);
    }
    LastAppliedServerWorldPosition.reset();
    LastServerWorldPoll = std::chrono::steady_clock::now();
    SetStage("entering world", 1.0f);
}

void FClientFrontendRuntime::BeginCharacterCreateRequest()
{
    int32 slot = 0;
    bool present = false;
    bool canCreate = false;
    std::wstring name;
    FCharacterCreationAppearance appearance;
    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.Character().SelectedSlotIndex();
        present = Ui.Character().SelectedCharacterPresent();
        canCreate = Ui.Character().SelectedCharacterCanCreate();
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
    int32 slot = 0;
    bool present = false;
    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.Character().SelectedSlotIndex();
        present = Ui.Character().SelectedCharacterPresent();
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
    FCharacterActionResult& result = event->Result;
    AddStatusLine("character: " + result.Message);
    const bool mutation = event->Action != ECharacterNetworkAction::Enter;
    if (result.Ok && mutation)
    {
        const std::optional<FLoginProbeResult>& refresh = event->RefreshedLogin;
        if (refresh && refresh->CharacterSelectReady && refresh->Session)
        {
            AddStatusLine("character: refreshed slots after mutation; " + refresh->Message);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterSlots(refresh->CharacterSlots);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
            Ui.SetStage("character select ready", 1.0f);
        }
        else
        {
            const std::string message = refresh ? refresh->Message : "refresh was not started";
            AddStatusLine("character: mutation completed, but charlist refresh failed: " + message);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.Character().SetCharacterActionLocked(false);
            Ui.Input().SetMode(EUiRuntimeMode::Login);
            Ui.SetStage("login screen ready", 1.0f);
        }
    }
    else if (result.Ok)
    {
        ClanRuntime.Reset();
        MapRuntime.Reset();
        const bool clanChanged = ClanRuntime.InspectFrames(result.Frames);
        MapRuntime.InspectFrames(result.Frames);
        FGameWorldPosition spawn;
        if (result.ServerPosition)
        {
            spawn.X = result.ServerPosition->X;
            spawn.Y = result.ServerPosition->Y;
            spawn.Z = result.ServerPosition->Z;
            spawn.Angle = result.ServerPosition->Angle;
            AddStatusLine("character: server spawn applied (" + std::to_string(spawn.X) + ", " + std::to_string(spawn.Y) + ", " + std::to_string(spawn.Z) + ")");
        }
        else
        {
            spawn.X = SferaProtocol::DefaultServerSpawnX;
            spawn.Y = SferaProtocol::DefaultServerSpawnY;
            spawn.Z = SferaProtocol::DefaultServerSpawnZ;
            spawn.Angle = SferaProtocol::DefaultServerSpawnAngle;
            AddStatusLine("character: server spawn packet was not parsed; using SphereEmu default spawn (" + std::to_string(spawn.X) + ", " + std::to_string(spawn.Y) + ", " + std::to_string(spawn.Z) + ")");
        }
        {
            std::lock_guard<std::mutex> renderLock(RenderMutex);
            RenderDevice.SetInitialGameWorldPosition(spawn);
            if (NetworkComponent.HasGameTime()) { RenderDevice.SetServerGameTime(NetworkComponent.GameTimeFraction()); }
        }
        LastAppliedServerWorldPosition = spawn;
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetMapPlayerPosition(spawn.X, spawn.Z);
        const FMapRuntimeState& map = MapRuntime.State();
        Ui.SetMapDescriptor(map.SpriteName, map.Projection, map.HasProjection);
        if (NetworkComponent.HasGameTime()) { Ui.SetServerGameTime(NetworkComponent.GameTimeFraction(), NetworkComponent.GameDay(), NetworkComponent.GameMonth(), NetworkComponent.GameYear()); }
        Ui.SetClanAvailable(clanChanged ? ClanRuntime.State().Available : false);
        Ui.Character().SetCharacterActionLocked(false);
        Ui.Input().SetMode(EUiRuntimeMode::Game);
        Ui.SetStage("game session active", 1.0f);
    }
    else
    {
        const char* stage = event->Action == ECharacterNetworkAction::Create ? "character create failed" : event->Action == ECharacterNetworkAction::Delete ? "character delete failed" : "character enter failed";
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.Character().SetCharacterActionLocked(false);
        Ui.Input().SetMode(EUiRuntimeMode::CharacterSelect);
        Ui.SetStage(stage, 1.0f);
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

    while (Window.IsOpen() && Window.PumpMessages())
    {
        FInputSnapshot input = Window.ConsumeInputFrame();
        RECT client{};

        if (Window.Handle())
        {
            GetClientRect(Window.Handle(), &client);
        }

        bool inputChanged = false;
        std::string action;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            inputChanged = Ui.Input().HandleInputFrame(input, client, Log);
            action = Ui.ConsumeLastAction();
        }

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

    return FStatus::Ok();
}

void FClientFrontendRuntime::Shutdown()
{
    NetworkComponent.Shutdown();
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


