#include "Client/ClientFrontendRuntime.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <system_error>
#include <vector>

FClientFrontendRuntime::FClientFrontendRuntime(FLogger* logger) : Log(logger), Session(logger) {}
FClientFrontendRuntime::~FClientFrontendRuntime()
{
    Shutdown();
}

std::wstring FClientFrontendRuntime::Utf8ToWide(const std::string& text)
{
    if (text.empty()) { return {}; }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

    if (required <= 0) { return {}; }

    std::wstring out(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), required);
    return out;
}

namespace
{
    std::filesystem::path SavedLoginPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD count = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        std::filesystem::path root = count > 0 ? std::filesystem::path(buffer).parent_path() : std::filesystem::current_path();
        return root / "sfera_login.cache";
    }
}

void FClientFrontendRuntime::DrawLoadingFrame(HDC__* dc, const tagRECT& rect)
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
    if (CharacterThread.joinable())
    {
        CharacterThread.join();
    }

    if (ActiveServerSession)
    {
        ActiveServerSession->Close();
    }

    ActiveServerSession.reset();
    CharacterActionInProgress.store(false);
}

FLoginProbeResult FClientFrontendRuntime::RefreshCharacterSelectSession(const std::wstring& login, const std::wstring& password, int32 timeoutMs)
{
    FLoginProbeResult result;

    if (!Endpoint) { result.Message = "endpoint not configured"; return result; }

    const FEndpoint endpoint = *Endpoint;
    const FCharacterAppearanceRules rules = AppearanceRules;
    constexpr int delays[] =
    {
        150, 350, 700
    };

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(delays[attempt]));
        result = ProbeLoginServer(endpoint, login, password, rules, timeoutMs);

        if (result.CharacterSelectReady && result.Session)
        {
            std::ostringstream out;
            out << result.Message << "; refresh attempt=" << (attempt + 1);
            result.Message = out.str();
            return result;
        }
    }

    return result;
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

    Window.SetPaintCallback([this](HDC__* dc, const tagRECT& rect)
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
    Endpoint = desc.Endpoint;
    {
        std::lock_guard<std::mutex> lock(SessionMutex);
        Session.Configure(Endpoint);
    }

    if (Endpoint)
    {
        AddStatusLine("network: endpoint configured " + Endpoint->Host + ":" + std::to_string(Endpoint->Port));
    }
    else
    {
        AddStatusLine("network: endpoint not found");
    }
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

FStatus FClientFrontendRuntime::InitializeUiResources(const FResourceManager& resources, const FUiBootstrapDesc& desc)
{
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }

    std::lock_guard<std::recursive_mutex> lock(UiMutex);
    FStatus status = Ui.Initialize(resources, desc, Log);

    if (!status.IsOk()) { return status; }

    Ui.SetCharacterAppearanceRules(AppearanceRules);
    LoadSavedLogin();
    Ui.SetStage("login screen ready", 1.0f);
    RepaintDirty = true;
    return FStatus::Ok();
}

FStatus FClientFrontendRuntime::InitializeD3D9(const FResourceManager& resources)
{
    if (!ShellCreated) { return FStatus::Error(EStatusCode::RuntimeError, "frontend shell is not created"); }

    if (D3DInitialized.load()) { return FStatus::Ok(); }

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
        RenderDevice.PreloadUiTextures(resources, Ui, Log);
    }
    RenderDevice.InspectShaderResources(resources, Log);
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

void FClientFrontendRuntime::RenderFrame()
{
    if (!ShellCreated || !Window.Handle()) { return; }

    if (!D3DInitialized.load() || !RenderResources) { return; }

    RECT client{};
    GetClientRect(Window.Handle(), &client);
    std::lock_guard<std::recursive_mutex> uiLock(UiMutex);
    std::lock_guard<std::mutex> renderLock(RenderMutex);
    FStatus status = RenderDevice.RenderUiDesktop(*RenderResources, Ui, client, Log);

    if (!status.IsOk() && Log)
    {
        Log->Warning("D3D9 render failed: " + status.Message());
    }
}

void FClientFrontendRuntime::ProcessUiAction(const std::string& action)
{
    if (action.empty()) { return; }

    AddStatusLine("ui: " + action);

    if (action == "quit_requested") { Shutdown(); return; }

    if (action == "save_login_on") { std::lock_guard<std::recursive_mutex> lock(UiMutex); StoreSavedLogin(true, Ui.ActionState().LoginText, Ui.ActionState().PasswordText); return; }

    if (action == "save_login_off")
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        StoreSavedLogin(false, {}, {});
        return;
    }

    if (action == "login_requested") { BeginLoginRequest(); return; }

    if (action == "registration_requested") { AddStatusLine(Settings.RegistrationUrl.empty() ? "registration: URL absent in connectn.cfg" : "registration: " + Settings.RegistrationUrl); return; }

    if (action.rfind("character_slot_", 0) == 0) { AddStatusLine("character: selected slot " + action.substr(15)); return; }

    if (action == "character_enter_requested") { BeginCharacterEnterRequest(); return; }

    if (action == "character_create_confirmed") { BeginCharacterCreateRequest(); return; }

    if (action == "character_create_dialog" || action == "character_delete_dialog" || action == "character_exit_dialog") { RepaintDirty = true; return; }

    if (action == "character_create_cancelled" || action == "character_delete_cancelled" || action == "character_back_cancelled" || action == "modal_closed") { RepaintDirty = true; return; }

    if (action == "character_delete_name_required") { AddStatusLine("character: type selected character name to confirm delete"); RepaintDirty = true; return; }

    if (action == "character_back_requested") { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.ShowExitConfirmation(); RepaintDirty = true; return; }

    if (action == "character_back_confirmed")
    {
        CloseActiveServerSession();
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
            Ui.SetMode(EUiRuntimeMode::Login);
            Ui.SetStage("login screen ready", 1.0f);
        }
        SetStage("login screen ready", 1.0f);
        return;
    }

    if (action == "character_delete_requested") { std::lock_guard<std::recursive_mutex> lock(UiMutex); Ui.ShowDeleteConfirmation(); RepaintDirty = true; return; }

    if (action == "character_delete_confirmed") { BeginCharacterDeleteRequest(); return; }
}

void FClientFrontendRuntime::BeginLoginRequest()
{
    if (LoginInProgress.exchange(true)) { return; }

    std::string login;
    std::string password;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        login = Ui.ActionState().LoginText;
        password = Ui.ActionState().PasswordText;
    }

    if (!Endpoint) { LoginInProgress.store(false); AddStatusLine("network: endpoint not configured"); SetStage("login failed", 1.0f); return; }

    if (login.empty() || password.empty()) { LoginInProgress.store(false); AddStatusLine("login: enter account and password"); SetStage("waiting for login", 1.0f); return; }

    if (LoginThread.joinable())
    {
        LoginThread.join();
    }

    SetStage("connecting to " + Endpoint->Host + ":" + std::to_string(Endpoint->Port), 1.0f);
    const FEndpoint endpoint = *Endpoint;
    const std::wstring wideLogin = Utf8ToWide(login);
    const std::wstring widePassword = Utf8ToWide(password);
    const FCharacterAppearanceRules rules = AppearanceRules;
    LoginThread = std::thread([this, endpoint, wideLogin, widePassword, rules]()
    {
        FLoginProbeResult result = ProbeLoginServer(endpoint, wideLogin, widePassword, rules, 2500);
        {
            std::lock_guard<std::mutex> lock(LoginMutex);
            PendingLoginResult = std::move(result);
        }
        LoginInProgress.store(false);
    });
}

void FClientFrontendRuntime::PollLoginResult()
{
    std::optional<FLoginProbeResult> result;
    {
        std::lock_guard<std::mutex> lock(LoginMutex);

        if (PendingLoginResult)
        {
            result = std::move(PendingLoginResult);
            PendingLoginResult.reset();
        }
    }

    if (!result) { return; }

    AddStatusLine("network: " + result->Message);

    if (result->CharacterSelectReady)
    {
        ActiveServerSession = result->Session;
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            const auto& state = Ui.ActionState();
            StoreSavedLogin(state.SaveLogin, state.LoginText, state.PasswordText);
            Ui.SetCharacterSlots(result->CharacterSlots);
            Ui.SetMode(EUiRuntimeMode::CharacterSelect);
            Ui.SetStage("character select ready", 1.0f);
        }
        RepaintDirty = true;
    }
    else if (result->Connected)
    {
        SetStage("server answered", 1.0f);
    }
    else
    {
        SetStage("login failed", 1.0f);
    }

    if (Log)
    {
        Log->Info("login probe result: " + result->Message);
    }

    RepaintDirty = true;
}

void FClientFrontendRuntime::BeginCharacterEnterRequest()
{
    if (CharacterActionInProgress.exchange(true)) { return; }

    std::shared_ptr<FServerSession> session = ActiveServerSession;
    int32 slot = 0;
    bool present = false;
    bool canCreate = false;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.SelectedCharacterSlot();
        present = Ui.SelectedCharacterPresent();
        canCreate = Ui.SelectedCharacterCanCreate();
        Ui.SetCharacterActionLocked(true);
    }

    if (!session || !session->Connected())
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: server session is not active");
        return;
    }

    if (!present)
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine(canCreate ? "character: creation requires confirmation" : "character: selected slot is unavailable");
        return;
    }

    if (CharacterThread.joinable())
    {
        CharacterThread.join();
    }

    SetStage("entering world", 1.0f);
    CharacterThread = std::thread([this, session, slot]()
    {
        FCharacterActionResult selected = session->SelectCharacter(slot, 2500);
        FCharacterActionResult ack;

        if (selected.Ok)
        {
            ack = session->SendIngameAck(2500);
            ack.Message = selected.Message + "; " + ack.Message;
        }
        else
        {
            ack = selected;
        }

        {
            std::lock_guard<std::mutex> lock(CharacterMutex);
            PendingCharacterResult = std::move(ack);
            PendingCharacterRefreshResult.reset();
            PendingCharacterActionKind = 0;
        }
        CharacterActionInProgress.store(false);
    });
}

void FClientFrontendRuntime::BeginCharacterCreateRequest()
{
    if (CharacterActionInProgress.exchange(true)) { return; }

    std::shared_ptr<FServerSession> session = ActiveServerSession;
    int32 slot = 0;
    bool present = false;
    bool canCreate = false;
    std::wstring name;
    FCharacterCreationAppearance appearance;
    std::string loginText;
    std::string passwordText;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.SelectedCharacterSlot();
        present = Ui.SelectedCharacterPresent();
        canCreate = Ui.SelectedCharacterCanCreate();
        name = Ui.SelectedCharacterName();
        appearance = Ui.SelectedCharacterAppearance(AppearanceRules);
        loginText = Ui.ActionState().LoginText;
        passwordText = Ui.ActionState().PasswordText;
        Ui.SetCharacterActionLocked(true);
    }

    if (!session || !session->Connected())
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: server session is not active");
        return;
    }

    if (present || !canCreate)
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: selected slot is not creatable");
        return;
    }

    if (name.empty())
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: name is empty");
        return;
    }

    if (CharacterThread.joinable())
    {
        CharacterThread.join();
    }

    SetStage("creating character", 1.0f);
    const std::wstring wideLogin = Utf8ToWide(loginText);
    const std::wstring widePassword = Utf8ToWide(passwordText);
    CharacterThread = std::thread([this, session, slot, name, appearance, wideLogin, widePassword]() mutable
    {
        FCharacterActionResult created = session->CreateCharacter(slot, name, appearance, 2500);
        std::optional<FLoginProbeResult> refresh;

        if (created.Ok)
        {
            session->Close();
            refresh = RefreshCharacterSelectSession(wideLogin, widePassword, 2500);
        }

        {
            std::lock_guard<std::mutex> lock(CharacterMutex);
            PendingCharacterResult = std::move(created);
            PendingCharacterRefreshResult = std::move(refresh);
            PendingCharacterActionKind = 1;
            PendingCharacterSlot = slot;
            PendingCharacterName = name;
            PendingCharacterAppearance = appearance;
        }
        CharacterActionInProgress.store(false);
    });
}

void FClientFrontendRuntime::BeginCharacterDeleteRequest()
{
    if (CharacterActionInProgress.exchange(true)) { return; }

    std::shared_ptr<FServerSession> session = ActiveServerSession;
    int32 slot = 0;
    bool present = false;
    std::string loginText;
    std::string passwordText;
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        slot = Ui.SelectedCharacterSlot();
        present = Ui.SelectedCharacterPresent();
        loginText = Ui.ActionState().LoginText;
        passwordText = Ui.ActionState().PasswordText;
        Ui.SetCharacterActionLocked(true);
    }

    if (!session || !session->Connected())
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: server session is not active");
        return;
    }

    if (!present)
    {
        CharacterActionInProgress.store(false);
        {
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
        }
        AddStatusLine("character: selected slot is empty");
        return;
    }

    if (CharacterThread.joinable())
    {
        CharacterThread.join();
    }

    SetStage("deleting character", 1.0f);
    const std::wstring wideLogin = Utf8ToWide(loginText);
    const std::wstring widePassword = Utf8ToWide(passwordText);
    CharacterThread = std::thread([this, session, slot, wideLogin, widePassword]() mutable
    {
        FCharacterActionResult deleted = session->DeleteCharacter(slot, 2500);
        std::optional<FLoginProbeResult> refresh;

        if (deleted.Ok)
        {
            session->Close();
            refresh = RefreshCharacterSelectSession(wideLogin, widePassword, 2500);
        }

        {
            std::lock_guard<std::mutex> lock(CharacterMutex);
            PendingCharacterResult = std::move(deleted);
            PendingCharacterRefreshResult = std::move(refresh);
            PendingCharacterActionKind = 2;
            PendingCharacterSlot = slot;
            PendingCharacterName.clear();
            PendingCharacterAppearance = {};
        }
        CharacterActionInProgress.store(false);
    });
}

void FClientFrontendRuntime::PollCharacterResult()
{
    std::optional<FCharacterActionResult> result;
    std::optional<FLoginProbeResult> refresh;
    int32 actionKind = 0;
    {
        std::lock_guard<std::mutex> lock(CharacterMutex);

        if (PendingCharacterResult)
        {
            result = std::move(PendingCharacterResult);
            PendingCharacterResult.reset();
            refresh = std::move(PendingCharacterRefreshResult);
            PendingCharacterRefreshResult.reset();
            actionKind = PendingCharacterActionKind;
            PendingCharacterActionKind = 0;
        }
    }

    if (!result) { return; }

    AddStatusLine("character: " + result->Message);

    if (result->Ok && (actionKind == 1 || actionKind == 2))
    {
        if (refresh && refresh->CharacterSelectReady && refresh->Session)
        {
            ActiveServerSession = refresh->Session;
            AddStatusLine("character: refreshed slots after mutation; " + refresh->Message);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterSlots(refresh->CharacterSlots);
            Ui.SetCharacterActionLocked(false);
            Ui.SetMode(EUiRuntimeMode::CharacterSelect);
            Ui.SetStage("character select ready", 1.0f);
        }
        else
        {
            ActiveServerSession.reset();
            const std::string message = refresh ? refresh->Message : "refresh was not started";
            AddStatusLine("character: mutation completed, but charlist refresh failed: " + message);
            std::lock_guard<std::recursive_mutex> lock(UiMutex);
            Ui.SetCharacterActionLocked(false);
            Ui.SetMode(EUiRuntimeMode::Login);
            Ui.SetStage("login screen ready", 1.0f);
        }
    }
    else if (result->Ok)
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetCharacterActionLocked(false);
        Ui.SetMode(EUiRuntimeMode::Game);
        Ui.SetStage("game session active", 1.0f);
    }
    else
    {
        std::lock_guard<std::recursive_mutex> lock(UiMutex);
        Ui.SetCharacterActionLocked(false);
        Ui.SetMode(EUiRuntimeMode::CharacterSelect);
        Ui.SetStage(actionKind == 1 ? "character create failed" : actionKind == 2 ? "character delete failed" : "character enter failed", 1.0f);
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
    auto lastRender = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

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
            inputChanged = Ui.HandleInputFrame(input, client, Log);
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

        {
            std::lock_guard<std::mutex> lock(SessionMutex);
            Session.Tick();
        }
        UpdateStageFromSession();
        PollLoginResult();
        PollCharacterResult();
        auto now = std::chrono::steady_clock::now();

        if (D3DInitialized.load() && (RepaintDirty || now - lastRender >= std::chrono::milliseconds(16)))
        {
            RenderFrame();
            lastRender = now;
            RepaintDirty = false;
        }
        else if (RepaintDirty)
        {
            RequestRepaintThrottled();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (Log)
    {
        Log->Info("frontend event loop finished");
    }

    return FStatus::Ok();
}

void FClientFrontendRuntime::Shutdown()
{
    if (LoginThread.joinable())
    {
        LoginThread.join();
    }

    if (CharacterThread.joinable())
    {
        CharacterThread.join();
    }

    ActiveServerSession.reset();
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

void FClientFrontendRuntime::UpdateStageFromSession()
{
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

    if (stage == EClientSessionStage::ProbeReceiving)
    {
        SetStage(stageText, 1.0f);
    }
    else if (stage == EClientSessionStage::Connected)
    {
        SetStage("connected; awaiting protocol", 1.0f);
    }
    else if (stage == EClientSessionStage::Failed)
    {
        SetStage("network probe failed", 1.0f);
    }
}
