#include "UI/UiRuntime.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include <cmath>
#include <cstdio>
#include <numeric>

namespace
{
uint64 GameUiKey(std::string_view window, int32 controlId);
}

FUiRuntime::FUiRuntime() : CharacterRuntime(*this), InputRuntime(*this) {}

FUiRuntime::~FUiRuntime()
{
    if (DeferredGameUiFuture.valid()) { DeferredGameUiFuture.wait(); }
}

FStatus FUiRuntime::Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger)
{
    Bootstrap = desc;
    SoundOptionVolume = std::clamp(Bootstrap.SoundVolume, 0.0f, 1.0f);
    MusicOptionVolume = std::clamp(Bootstrap.MusicVolume, 0.0f, 1.0f);
    auto strings = DocumentParser.LoadStringTableFromResource(resources, Bootstrap.StringsResource);

    if (!strings.IsOk()) { return strings.Status(); }

    auto connection = DocumentParser.LoadWindowFromResource(resources, Bootstrap.ConnectionWindowResource);

    if (!connection.IsOk()) { return connection.Status(); }

    auto pickPerson = DocumentParser.LoadWindowFromResource(resources, Bootstrap.PickPersonWindowResource);

    if (!pickPerson.IsOk() && logger)
    {
        logger->Warning("character-select UI is not available: " + pickPerson.Status().Message());
    }

    auto createPerson = DocumentParser.LoadWindowFromResource(resources, Bootstrap.CreatePersonWindowResource);

    if (!createPerson.IsOk() && logger)
    {
        logger->Warning("create-character UI is not available: " + createPerson.Status().Message());
    }

    auto deleteCharacter = DocumentParser.LoadWindowFromResource(resources, Bootstrap.DeleteCharacterWindowResource);

    if (!deleteCharacter.IsOk() && logger)
    {
        logger->Warning("delete-character UI is not available: " + deleteCharacter.Status().Message());
    }

    auto connectMessage = DocumentParser.LoadWindowFromResource(resources, Bootstrap.ConnectMessageWindowResource);

    if (!connectMessage.IsOk() && logger)
    {
        logger->Warning("confirmation UI is not available: " + connectMessage.Status().Message());
    }

    auto message = DocumentParser.LoadWindowFromResource(resources, Bootstrap.MessageWindowResource);

    if (!message.IsOk() && logger)
    {
        logger->Warning("generic message UI is not available: " + message.Status().Message());
    }

    if (!resources.Catalog().FindByLogicalName(Bootstrap.LoginBackgroundTexture)) { return FStatus::Error(EStatusCode::NotFound, "login background texture is not cataloged: " + Bootstrap.LoginBackgroundTexture); }

    StringTable = std::move(strings.Value());
    StringTable["UISTR_WT_STATINFO_NATIVE"] = Bootstrap.Lang == 1 ? "Characteristics" : "Характеристики";
    Connection = std::move(connection.Value());

    if (pickPerson.IsOk())
    {
        PickPerson = std::move(pickPerson.Value());
    }

    if (createPerson.IsOk())
    {
        CreatePerson = std::move(createPerson.Value());
    }

    if (deleteCharacter.IsOk())
    {
        DeleteCharacter = std::move(deleteCharacter.Value());
    }

    if (connectMessage.IsOk())
    {
        ConnectMessage = std::move(connectMessage.Value());
    }

    if (message.IsOk())
    {
        Message = std::move(message.Value());
    }

    if (DeferredGameUiFuture.valid()) { DeferredGameUiFuture.wait(); }
    DeferredGameUiFuture = {};
    DeferredGameUiLogger = logger;
    DeferredGameUiIntegrated = true;
    DeferredWindowVisibility.clear();
    GameWindowDefs.clear();
    GameWindowVisible.clear();
    GameWindowOrder.clear();
    GameWindowLookup.clear();
    GameWindowPositions.clear();
    GameWindowPositionOverrides.clear();
    GameControlChecks.clear();
    GameEditValues.clear();
    const std::array<std::string_view, 9> excludedFiles{"strings.ui", "strings_e.ui", "strings_i.ui", "strings_p.ui", "sprites.ui", "loading.ui", "loadingprogress.ui", "loadscreen.ui", "connection.ui"};
    const std::array<std::string_view, 4> criticalFiles{"system_left.ui", "system_right.ui", "chat_st2.ui", "chat_sys.ui"};
    std::vector<std::string> criticalResources;
    std::vector<std::string> deferredResources;
    for (const FFileRecord& record : resources.Catalog().FindByKind(EResourceKind::Ui))
    {
        std::string logical = record.RelativePath.generic_string();
        const std::string lower = Common::ToLower(logical);
        if (!lower.starts_with("effects/") || !lower.ends_with(".ui")) { continue; }
        const std::string file = Common::ToLower(record.RelativePath.filename().string());
        if (std::find(excludedFiles.begin(), excludedFiles.end(), file) != excludedFiles.end()) { continue; }
        if (std::find(criticalFiles.begin(), criticalFiles.end(), file) != criticalFiles.end()) { criticalResources.push_back(std::move(logical)); }
        else { deferredResources.push_back(std::move(logical)); }
    }
    std::sort(criticalResources.begin(), criticalResources.end());
    std::sort(deferredResources.begin(), deferredResources.end());
    for (const std::string& resourceName : criticalResources)
    {
        auto windows = DocumentParser.LoadWindowsFromResource(resources, resourceName);
        if (!windows.IsOk())
        {
            if (logger) { logger->Warning("game UI resource is not available: " + resourceName + "; " + windows.Status().Message()); }
            continue;
        }
        for (FUiWindowDef& window : windows.Value()) { RegisterGameWindow(std::move(window), true); }
    }
    if (!deferredResources.empty())
    {
        DeferredGameUiIntegrated = false;
        DeferredGameUiFuture = std::async(std::launch::async, [&resources, resourceNames = std::move(deferredResources)]() mutable
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            FDeferredGameUiBatch batch;
            FUiDocumentParser parser;
            for (const std::string& resourceName : resourceNames)
            {
                auto windows = parser.LoadWindowsFromResource(resources, resourceName);
                if (!windows.IsOk())
                {
                    batch.Warnings.push_back("game UI resource is not available: " + resourceName + "; " + windows.Status().Message());
                    continue;
                }
                for (FUiWindowDef& window : windows.Value()) { batch.Windows.push_back(std::move(window)); }
            }
            return batch;
        });
    }

    Ready = true;
    AddStatusLine("ui: connection window loaded, controls=" + std::to_string(Connection.Controls.size()) + ", sprites=" + std::to_string(Connection.Sprites.size()));

    if (!PickPerson.Name.empty())
    {
        AddStatusLine("ui: character window loaded, controls=" + std::to_string(PickPerson.Controls.size()) + ", sprites=" + std::to_string(PickPerson.Sprites.size()));
    }

    if (!CreatePerson.Name.empty())
    {
        AddStatusLine("ui: create window loaded, controls=" + std::to_string(CreatePerson.Controls.size()));
    }

    if (!DeleteCharacter.Name.empty())
    {
        AddStatusLine("ui: delete window loaded, controls=" + std::to_string(DeleteCharacter.Controls.size()));
    }

    if (!ConnectMessage.Name.empty())
    {
        AddStatusLine("ui: confirm window loaded, controls=" + std::to_string(ConnectMessage.Controls.size()));
    }

    if (!Message.Name.empty())
    {
        AddStatusLine("ui: message window loaded, controls=" + std::to_string(Message.Controls.size()));
    }

    if (logger)
    {
        logger->Info("UI runtime initialized: strings=" + std::to_string(StringTable.size()) + ", connection=" + Connection.Name + ", controls=" + std::to_string(Connection.Controls.size()) + ", pick_person=" + PickPerson.Name + ", create_person=" + CreatePerson.Name + ", connect_message=" + ConnectMessage.Name + ", message=" + Message.Name + ", game_windows=" + std::to_string(GameWindowDefs.size()));
    }

    return FStatus::Ok();
}

void FUiRuntime::RegisterGameWindow(FUiWindowDef window, bool initiallyVisible)
{
    const std::string key = window.NameKey.empty() ? Common::ToLower(window.Name) : window.NameKey;
    const bool bootstrapWindow = Common::EqualsNoCase(key, Connection.NameKey) || Common::EqualsNoCase(key, PickPerson.NameKey) || Common::EqualsNoCase(key, CreatePerson.NameKey) || Common::EqualsNoCase(key, DeleteCharacter.NameKey) || Common::EqualsNoCase(key, ConnectMessage.NameKey) || Common::EqualsNoCase(key, Message.NameKey);
    if (key.empty() || bootstrapWindow || GameWindowLookup.contains(key)) { return; }
    const size_t index = GameWindowDefs.size();
    bool visible = initiallyVisible && (key == "system_left" || key == "system_right" || key == "chat_st2" || key == "chat_sys");
    if (auto requested = DeferredWindowVisibility.find(key); requested != DeferredWindowVisibility.end())
    {
        visible = requested->second;
        DeferredWindowVisibility.erase(requested);
    }
    GameWindowLookup.emplace(key, index);
    GameWindowVisible.push_back(visible);
    GameWindowOrder.push_back(index);
    GameWindowPositions.push_back({});
    GameWindowPositionOverrides.push_back(false);
    if (key == "statinfo" || key == "statinfo_n") { window.TextKey = "UISTR_WT_STATINFO_NATIVE"; }
    if (key == "chat_st2")
    {
        for (FUiControlDef& control : window.Controls)
        {
            if (control.Id == 1) { control.Font = Bootstrap.ChatListFont; }
            else if (control.Id == 3) { control.Font = Bootstrap.ChatEditFont; }
        }
    }
    GameWindowDefs.push_back(std::move(window));
}

void FUiRuntime::PumpDeferredGameWindows(bool waitForCompletion)
{
    if (DeferredGameUiIntegrated || !DeferredGameUiFuture.valid()) { return; }
    if (!waitForCompletion && DeferredGameUiFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) { return; }
    FDeferredGameUiBatch batch = DeferredGameUiFuture.get();
    for (std::string& warning : batch.Warnings) { if (DeferredGameUiLogger) { DeferredGameUiLogger->Warning(std::move(warning)); } }
    const size_t previousCount = GameWindowDefs.size();
    for (FUiWindowDef& window : batch.Windows) { RegisterGameWindow(std::move(window), false); }
    DeferredGameUiIntegrated = true;
    if (DeferredGameUiLogger) { DeferredGameUiLogger->Info("deferred game UI ready: added=" + std::to_string(GameWindowDefs.size() - previousCount) + ", total=" + std::to_string(GameWindowDefs.size())); }
}

void FUiRuntime::SetStage(std::string stage, float progress)
{
    CurrentStage = std::move(stage);
    CurrentProgress = std::clamp(progress, 0.0f, 1.0f);
}

void FUiRuntime::AddStatusLine(std::string line)
{
    Status.push_back(std::move(line));

    if (Status.size() > SferaUi::MaxStatusLines)
    {
        Status.erase(Status.begin());
    }
}

std::string FUiRuntime::ResolveText(std::string_view key) const
{
    auto it = StringTable.find(std::string(key));

    if (it == StringTable.end()) { return {}; }

    return it->second;
}

std::string FUiRuntime::ConsumeLastAction()
{
    std::string action = std::move(Actions.LastAction);
    Actions.LastAction.clear();
    return action;
}

std::optional<size_t> FUiRuntime::FindGameWindowIndex(std::string_view name) const
{
    auto it = GameWindowLookup.find(name);
    if (it != GameWindowLookup.end()) { return it->second; }
    const std::string lower = Common::ToLower(name);
    it = GameWindowLookup.find(lower);
    return it == GameWindowLookup.end() ? std::nullopt : std::optional<size_t>(it->second);
}

bool FUiRuntime::IsGameWindowVisible(std::string_view name) const
{
    const std::optional<size_t> index = FindGameWindowIndex(name);
    if (index) { return *index < GameWindowVisible.size() && GameWindowVisible[*index]; }
    const std::string key = Common::ToLower(name);
    auto requested = DeferredWindowVisibility.find(key);
    return requested != DeferredWindowVisibility.end() && requested->second;
}

bool FUiRuntime::SetGameWindowVisible(std::string_view name, bool visible)
{
    const std::optional<size_t> index = FindGameWindowIndex(name);
    if (!index)
    {
        if (!DeferredGameUiIntegrated) { DeferredWindowVisibility[Common::ToLower(name)] = visible; return true; }
        return false;
    }
    if (*index >= GameWindowVisible.size() || GameWindowVisible[*index] == visible) { return false; }
    GameWindowVisible[*index] = visible;
    if (visible) { BringGameWindowToFront(*index); }
    if (!visible && Actions.HoverWindowIndex == static_cast<int32>(*index)) { Actions.HoverWindowIndex = -1; Actions.HoverControlId = 0; }
    if (!visible && Actions.PressedWindowIndex == static_cast<int32>(*index)) { Actions.PressedWindowIndex = -1; Actions.PressedControlId = 0; }
    if (!visible && Actions.FocusedWindowIndex == static_cast<int32>(*index))
    {
        Actions.FocusedWindowIndex = -1;
        Actions.FocusedControlId = 0;
        GameChatFocused = false;
    }
    return true;
}

bool FUiRuntime::ToggleGameWindow(std::string_view name)
{
    return SetGameWindowVisible(name, !IsGameWindowVisible(name));
}

void FUiRuntime::BringGameWindowToFront(size_t index)
{
    if (index >= GameWindowDefs.size() || !GameWindowDefs[index].CanGoTop) { return; }
    auto it = std::find(GameWindowOrder.begin(), GameWindowOrder.end(), index);
    if (it == GameWindowOrder.end() || std::next(it) == GameWindowOrder.end()) { return; }
    GameWindowOrder.erase(it);
    GameWindowOrder.push_back(index);
}

bool FUiRuntime::CloseTopGameWindow()
{
    for (auto it = GameWindowOrder.rbegin(); it != GameWindowOrder.rend(); ++it)
    {
        const size_t index = *it;
        if (index >= GameWindowDefs.size() || index >= GameWindowVisible.size() || !GameWindowVisible[index]) { continue; }
        if (!GameWindowDefs[index].EscapeHandle) { continue; }
        return SetGameWindowVisible(GameWindowDefs[index].Name, false);
    }
    return false;
}

void FUiRuntime::ResetGameWindows()
{
    static constexpr std::array<std::string_view, 4> visibleByDefault{"system_left", "system_right", "chat_st2", "chat_sys"};
    std::fill(GameWindowVisible.begin(), GameWindowVisible.end(), false);
    GameWindowOrder.clear();
    for (size_t index = 0; index < GameWindowDefs.size(); ++index) { GameWindowOrder.push_back(index); }
    for (std::string_view name : visibleByDefault)
    {
        const std::optional<size_t> index = FindGameWindowIndex(name);
        if (index && *index < GameWindowVisible.size()) { GameWindowVisible[*index] = true; }
    }
    GameChat.clear();
    GameControlChecks.clear();
    GameControlChecks[GameUiKey("interface_options", 7)] = true;
    GameControlChecks[GameUiKey("interface_options", 8)] = true;
    GameControlChecks[GameUiKey("interface_options", 9)] = true;
    GameControlChecks[GameUiKey("interface_options", 11)] = true;
    GameControlChecks[GameUiKey("interface_options", 12)] = true;
    GameControlChecks[GameUiKey("interface_options", 15)] = true;
    GameControlChecks[GameUiKey("interface_options", 16)] = true;
    GameControlChecks[GameUiKey("interface_options", 19)] = true;
    GameControlChecks[GameUiKey("interface_options", 21)] = true;
    GameEditValues.clear();
    GameControlValues.clear();
    GameControlHidden.clear();
    GameControlDisabled.clear();
    GameChatLines.clear();
    GameChatMode = 0;
    GroupAvailable = false;
    UiAnimationTime = 0.0f;
    StatAllocationDelta.fill(0);
    ActiveMapSprite = "mhp1";
    MapProjection.fill(0);
    HasMapProjection = false;
    HasMapPlayerPosition = false;
    ClockDigital = false;
    CurrentHelpTopic.clear();
    JournalEntries.clear();
    JournalSelected = -1;
    JournalEditing = false;
    GameChatFocused = false;
    GameDragWindowIndex = -1;
    Actions.HoverControlId = 0;
    Actions.PressedControlId = 0;
    Actions.FocusedControlId = 0;
    Actions.HoverWindowIndex = -1;
    Actions.PressedWindowIndex = -1;
    Actions.FocusedWindowIndex = -1;
}

FUiRectF FUiRuntime::BuildGameWindowRect(size_t index, const RECT& clientRect) const
{
    if (index >= GameWindowDefs.size()) { return {}; }
    FUiRectF rect = InputRuntime.BuildWindowRect(GameWindowDefs[index], clientRect);
    if (index < GameWindowPositionOverrides.size() && GameWindowPositionOverrides[index])
    {
        rect.X = static_cast<float>(GameWindowPositions[index].X);
        rect.Y = static_cast<float>(GameWindowPositions[index].Y);
    }
    return rect;
}

bool FUiRuntime::IsGameControlChecked(std::string_view window, int32 controlId) const
{
    auto it = GameControlChecks.find(GameUiKey(window, controlId));
    return it != GameControlChecks.end() && it->second;
}

const std::string& FUiRuntime::GameEditText(std::string_view window, int32 controlId) const
{
    static const std::string empty;
    if (window.size() >= 4 && std::tolower(static_cast<unsigned char>(window[0])) == 'c' && std::tolower(static_cast<unsigned char>(window[1])) == 'h' && std::tolower(static_cast<unsigned char>(window[2])) == 'a' && std::tolower(static_cast<unsigned char>(window[3])) == 't') { return GameChat; }
    auto it = GameEditValues.find(GameUiKey(window, controlId));
    return it == GameEditValues.end() ? empty : it->second;
}

bool FUiRuntime::IsGameTextInputFocused() const
{
    if (CurrentMode != EUiRuntimeMode::Game || Actions.FocusedWindowIndex < 0 || Actions.FocusedControlId == 0) { return false; }
    const size_t index = static_cast<size_t>(Actions.FocusedWindowIndex);
    if (index >= GameWindowDefs.size()) { return false; }
    const auto it = std::find_if(GameWindowDefs[index].Controls.begin(), GameWindowDefs[index].Controls.end(), [&](const FUiControlDef& control) { return control.Id == Actions.FocusedControlId; });
    return it != GameWindowDefs[index].Controls.end() && (Common::EqualsNoCase(it->ClassId, "EDIT") || Common::EqualsNoCase(it->ClassId, "HTEDIT") || Common::EqualsNoCase(it->ClassId, "RICHEDIT"));
}

void FUiRuntime::ToggleGameControlChecked(std::string_view window, const FUiControlDef& control)
{
    if (control.Class == EUiControlClass::RadioButton && control.Group != 0)
    {
        const std::optional<size_t> index = FindGameWindowIndex(window);
        if (index)
        {
            for (const FUiControlDef& candidate : GameWindowDefs[*index].Controls)
            {
                if (candidate.Class == EUiControlClass::RadioButton && candidate.Group == control.Group) { GameControlChecks[GameUiKey(window, candidate.Id)] = false; }
            }
        }
        GameControlChecks[GameUiKey(window, control.Id)] = true;
        return;
    }
    const uint64 key = GameUiKey(window, control.Id);
    GameControlChecks[key] = !GameControlChecks[key];
}

std::string FUiRuntime::TakeGameChatDraft()
{
    std::string result = std::move(GameChat);
    GameChat.clear();
    return result;
}

const FUiWindowDef& FUiRuntime::ActiveModalWindow() const
{
    if ((Modal == EUiModalDialog::CharacterCreate || Modal == EUiModalDialog::CharacterExit || Modal == EUiModalDialog::GameExit) && !Message.Name.empty()) { return Message; }

    if (Modal == EUiModalDialog::CharacterDelete && !DeleteCharacter.Name.empty()) { return DeleteCharacter; }

    return ConnectMessage.Name.empty() ? Connection : ConnectMessage;
}

void FUiRuntime::SetLoginCredentials(std::string login, std::string password, bool saveLogin)
{
    Actions.LoginText = std::move(login);
    Actions.PasswordText = std::move(password);
    Actions.SaveLogin = saveLogin;
}

void FUiRuntime::ShowExitConfirmation()
{
    Modal = EUiModalDialog::CharacterExit;
    ResetModalAnimation();
    ModalText = Bootstrap.Lang == 1 ? "Exit to login screen?" : "Выйти на экран логина?";
    ModalEditText.clear();
    Actions.LastAction = "character_exit_dialog";
}

void FUiRuntime::ShowGameExitConfirmation()
{
    Modal = EUiModalDialog::GameExit;
    ResetModalAnimation();
    ModalText = Bootstrap.Lang == 1 ? "Leave the game world?" : "Выйти из игрового мира?";
    ModalEditText.clear();
    Actions.LastAction = "game_leave_dialog";
}

void FUiRuntime::ShowCreateConfirmation()
{
    Modal = EUiModalDialog::CharacterCreate;
    ResetModalAnimation();
    ModalText = Bootstrap.Lang == 1 ? "Create this character?" : "Создать этого персонажа?";
    ModalEditText.clear();
    Actions.LastAction = "character_create_dialog";
}

void FUiRuntime::ShowDeleteConfirmation()
{
    Modal = EUiModalDialog::CharacterDelete;
    ResetModalAnimation();
    const std::string name = Common::WideToUtf8(CharacterRuntime.SelectedCharacterName());
    ModalText = Bootstrap.Lang == 1 ? "Type character name to delete: " + name : "Введите имя персонажа для удаления: " + name;
    ModalEditText.clear();
    Actions.FocusedControlId = SferaUi::DeleteConfirmEditId;
    Actions.LastAction = "character_delete_dialog";
}

void FUiRuntime::ResetModalAnimation()
{
    ModalClosing = false;
    ModalAnimationTime = 0.0f;
}

void FUiRuntime::ClearModalState()
{
    Modal = EUiModalDialog::None;
    ModalClosing = false;
    ModalAnimationTime = 0.0f;
    ModalText.clear();
    ModalEditText.clear();
    Actions.PressedControlId = 0;
    Actions.PressedWindowIndex = -1;
}

const FUiPopupAnimationDesc& FUiRuntime::ActiveModalAnimation() const
{
    static const FUiPopupAnimationDesc none{};
    if (Modal == EUiModalDialog::None) { return none; }
    const FUiWindowDef& window = ActiveModalWindow();
    return ModalClosing ? window.HideEffect : window.ShowEffect;
}

void FUiRuntime::DismissModal()
{
    if (Modal == EUiModalDialog::None) { return; }
    const FUiPopupAnimationDesc& effect = ActiveModalAnimation();
    if (!ModalClosing && effect.IsValid())
    {
        ModalClosing = true;
        ModalAnimationTime = 0.0f;
        Actions.PressedControlId = 0;
        return;
    }
    ClearModalState();
}

void FUiRuntime::Tick(float deltaSeconds)
{
    PumpDeferredGameWindows(false);
    const float clampedDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    UiAnimationTime = std::fmod(UiAnimationTime + clampedDelta, 3600.0f);
    if (CurrentMode == EUiRuntimeMode::Game && HasServerGameTime)
    {
        const float previous = ServerGameTime;
        ServerGameTime = std::fmod(ServerGameTime + clampedDelta * 12.0f / 86400.0f, 1.0f);
        if (ServerGameTime < previous && ServerGameDay > 0) { ++ServerGameDay; }
    }
    if (Modal == EUiModalDialog::None) { return; }
    const FUiPopupAnimationDesc& effect = ActiveModalAnimation();
    if (!effect.IsValid())
    {
        if (ModalClosing) { ClearModalState(); }
        return;
    }
    ModalAnimationTime += clampedDelta;
    if (ModalClosing && ModalAnimationTime >= effect.Duration)
    {
        ClearModalState();
    }
}

float FUiRuntime::ModalAnimationProgress() const
{
    if (Modal == EUiModalDialog::None) { return 1.0f; }
    const FUiPopupAnimationDesc& effect = ActiveModalAnimation();
    if (!effect.IsValid()) { return 1.0f; }
    return std::clamp(ModalAnimationTime / std::max(0.001f, effect.Duration), 0.0f, 1.0f);
}

float FUiRuntime::ModalAnimationAlpha() const
{
    const FUiPopupAnimationDesc& effect = ActiveModalAnimation();
    if (!effect.IsValid()) { return 1.0f; }
    const float progress = ModalAnimationProgress();
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    if (effect.Effect == EUiPopupEffect::AlphaIn || effect.Effect == EUiPopupEffect::AlphaOut)
    {
        return ModalClosing ? 1.0f - eased : eased;
    }
    return 1.0f;
}

FUiRectF FUiRuntime::BuildAnimatedModalRect(const RECT& clientRect) const
{
    const FUiWindowDef& window = ActiveModalWindow();
    FUiRectF rect = InputRuntime.BuildWindowRect(window, clientRect);
    const FUiPopupAnimationDesc& effect = ActiveModalAnimation();
    if (!effect.IsValid()) { return rect; }
    const float progress = ModalAnimationProgress();
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const float amount = ModalClosing ? eased : 1.0f - eased;
    const float moveX = effect.OffsetX != 0.0f ? std::abs(effect.OffsetX) : rect.W;
    const float moveY = effect.OffsetY != 0.0f ? std::abs(effect.OffsetY) : rect.H;
    if (effect.Effect == EUiPopupEffect::MoveLeft) { rect.X -= moveX * amount; }
    else if (effect.Effect == EUiPopupEffect::MoveRight) { rect.X += moveX * amount; }
    else if (effect.Effect == EUiPopupEffect::MoveTop) { rect.Y -= moveY * amount; }
    else if (effect.Effect == EUiPopupEffect::MoveBottom) { rect.Y += moveY * amount; }
    return rect;
}

namespace
{
uint64 GameUiKey(std::string_view window, int32 controlId)
{
    uint32 hash = 2166136261u;
    for (unsigned char ch : window)
    {
        const unsigned char lower = static_cast<unsigned char>(std::tolower(ch));
        hash = (hash ^ static_cast<uint32>(lower)) * 16777619u;
    }
    return (static_cast<uint64>(hash) << 32) | static_cast<uint32>(controlId);
}

std::string JoinUiLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (const std::string& line : lines)
    {
        if (!result.empty()) { result.push_back('\n'); }
        result += line;
    }
    return result;
}

std::string FormatGameClock(float fraction, bool includeSeconds)
{
    int32 total = static_cast<int32>(std::floor((fraction - std::floor(fraction)) * 86400.0f)) % 86400;
    const int32 hours = total / 3600;
    const int32 minutes = (total / 60) % 60;
    const int32 seconds = total % 60;
    char buffer[24]{};
    if (includeSeconds) { std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds); }
    else { std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, minutes); }
    return buffer;
}
}

void FUiRuntime::AppendGameChatLine(std::string line, int32 mode)
{
    if (line.empty()) { return; }
    const int32 colorMode = mode < 0 ? GameChatMode : std::clamp(mode, 0, 4);
    static constexpr std::array<FUiColor, 5> colors{{{40, 90, 220, 255}, {30, 190, 90, 255}, {210, 110, 40, 255}, {40, 180, 200, 255}, {120, 185, 105, 255}}};
    GameChatLines.push_back({std::move(line), colors[static_cast<size_t>(colorMode)]});
    if (GameChatLines.size() > 120) { GameChatLines.erase(GameChatLines.begin(), GameChatLines.begin() + static_cast<std::ptrdiff_t>(GameChatLines.size() - 120)); }
}

void FUiRuntime::CycleGameChatMode()
{
    for (int32 attempt = 0; attempt < 5; ++attempt)
    {
        GameChatMode = (GameChatMode + 1) % 5;
        if ((GameChatMode != 3 || ClanAvailable) && (GameChatMode != 4 || GroupAvailable)) { break; }
    }
}

uint8 FUiRuntime::GameChatChannel() const
{
    static constexpr std::array<uint8, 5> channels{1, 2, 3, 4, 5};
    return channels[static_cast<size_t>(std::clamp(GameChatMode, 0, 4))];
}

FUiColor FUiRuntime::GameChatModeColor() const
{
    static constexpr std::array<FUiColor, 5> colors{{{40, 90, 220, 255}, {30, 190, 90, 255}, {210, 110, 40, 255}, {40, 180, 200, 255}, {120, 185, 105, 255}}};
    return colors[static_cast<size_t>(std::clamp(GameChatMode, 0, 4))];
}

bool FUiRuntime::IsTextCaretVisible() const
{
    return std::fmod(UiAnimationTime, 1.0f) < 0.5f;
}

void FUiRuntime::SetServerGameTime(float dayFraction, int32 day, int32 month, int32 year)
{
    ServerGameTime = dayFraction - std::floor(dayFraction);
    HasServerGameTime = true;
    if (day > 0) { ServerGameDay = day; }
    if (month > 0) { ServerGameMonth = month; }
    if (year > 0) { ServerGameYear = year; }
}

void FUiRuntime::ClearServerGameTime()
{
    ServerGameTime = 0.0f;
    HasServerGameTime = false;
    ServerGameDay = 0;
    ServerGameMonth = 0;
    ServerGameYear = 0;
}

std::string FUiRuntime::GameControlText(std::string_view windowName, const FUiControlDef& control) const
{
    const std::string window = Common::ToLower(std::string(windowName));
    const auto& slot = CharacterRuntime.SelectedSlotInfo();
    if (window == "system_right")
    {
        if (control.Id == 2)
        {
            if (ServerGameDay > 0 && ServerGameMonth > 0) { return std::to_string(ServerGameDay) + "." + std::to_string(ServerGameMonth) + "." + std::to_string(ServerGameYear); }
            return Bootstrap.Lang == 1 ? "Sphere time" : "Время Сферы";
        }
        if (control.Id == 3) { return HasServerGameTime ? FormatGameClock(ServerGameTime, false) : "--:--"; }
    }
    if (window == "chat_st2" && control.Id == 1)
    {
        std::vector<std::string> lines;
        lines.reserve(GameChatLines.size());
        for (const FGameChatLine& line : GameChatLines) { lines.push_back(line.Text); }
        return JoinUiLines(lines);
    }
    if (window == "statinfo" || window == "statinfo_n")
    {
        auto statValue = [&](int32 index, int32 value) { return value + StatAllocationDelta[static_cast<size_t>(index)]; };
        switch (control.Id)
        {
        case 3: return std::to_string(slot.TitleXp) + " / " + std::to_string(slot.TitleNextXp);
        case 4: return std::to_string(slot.DegreeXp) + " / " + std::to_string(slot.DegreeNextXp);
        case 5: return std::to_string(slot.CurrentHp) + " / " + std::to_string(slot.MaxHp);
        case 6: return std::to_string(slot.CurrentMp) + " / " + std::to_string(slot.MaxMp);
        case 7: return std::to_string(slot.CurrentSatiety) + " / " + std::to_string(slot.MaxSatiety);
        case 8: return slot.Present ? (Bootstrap.Lang == 1 ? "Title " : "Звание ") + std::to_string(slot.TitleId) + " (" + std::to_string(std::max(1, slot.TitleLevel)) + ")" : std::string{};
        case 9: return slot.Present ? (Bootstrap.Lang == 1 ? "Degree " : "Ремесло ") + std::to_string(slot.DegreeId) + " (" + std::to_string(std::max(1, slot.DegreeLevel)) + ")" : std::string{};
        case 10: return std::to_string(slot.PhysicalAttack);
        case 11: return std::to_string(slot.MagicalAttack);
        case 12: return std::to_string(slot.PhysicalDefense);
        case 13: return std::to_string(slot.MagicalDefense);
        case 14: return std::to_string(slot.TitleStats - std::accumulate(StatAllocationDelta.begin(), StatAllocationDelta.begin() + 4, 0));
        case 15: return std::to_string(slot.DegreeStats - std::accumulate(StatAllocationDelta.begin() + 4, StatAllocationDelta.end(), 0));
        case 16: return std::to_string(statValue(0, slot.Strength));
        case 17: return std::to_string(statValue(1, slot.Dexterity));
        case 18: return std::to_string(statValue(2, slot.Accuracy));
        case 19: return std::to_string(statValue(3, slot.Endurance));
        case 20: return std::to_string(statValue(4, slot.Fire));
        case 21: return std::to_string(statValue(5, slot.Water));
        case 22: return std::to_string(statValue(6, slot.Earth));
        case 23: return std::to_string(statValue(7, slot.Air));
        case 24: return Bootstrap.Lang == 1 ? "Karma: " + std::to_string(slot.Karma) : "Карма: " + std::to_string(slot.Karma);
        case 35: return slot.Present ? Common::WideToUtf8(slot.Name) : std::string{};
        default: break;
        }
    }
    if (window == "authors" && control.Id == 1)
    {
        return Bootstrap.Lang == 1 ? "Sphere\nOriginal game client and content: Nikita\nNative client reconstruction: community recon project\nMBC runtime replacement and UI integration: native C++ components" : "Сфера\nОригинальный клиент и игровые материалы: компания «Никита»\nРеконструкция нативного клиента: проект recon\nЗамена MBC и интеграция интерфейса: нативные C++-компоненты";
    }
    if (window == "quit" && control.Id == 3) { return Bootstrap.Lang == 1 ? "Leave the game world and return to login?" : "Выйти из игрового мира и вернуться на экран входа?"; }
    if (window == "help" && control.Id == 3)
    {
        std::string topic = CurrentHelpTopic.empty() ? "interface" : CurrentHelpTopic;
        std::string header = Bootstrap.Lang == 1 ? "Help: " : "Справка: ";
        std::string body = Bootstrap.Lang == 1 ? "WASD — movement\nShift — run\nSpace — jump\nRight mouse or Tab — camera\nI — inventory\nP — character\nC — characteristics\nM — map\nJ — journal\nEnter — chat\nEscape — close window or open menu" : "WASD — движение\nShift — бег\nПробел — прыжок\nПравая кнопка мыши или Tab — камера\nI — инвентарь\nP — персонаж\nC — характеристики\nM — карта\nJ — журнал\nEnter — чат\nEscape — закрыть окно или открыть меню";
        return header + topic + "\n\n" + body;
    }
    if (window == "control_options" && control.Id == 5)
    {
        return Bootstrap.Lang == 1 ? "Move forward                 W / Up\nMove backward              S / Down\nStrafe left                    A / Left\nStrafe right                  D / Right\nRun                              Shift\nJump                            Space\nCamera mode                Tab\nInventory                      I\nCharacter                      P\nCharacteristics              C\nMap                              M\nJournal                         J\nChat                              Enter\nMenu / close                 Escape" : "Движение вперёд          W / Стрелка вверх\nДвижение назад           S / Стрелка вниз\nШаг влево                     A / Стрелка влево\nШаг вправо                   D / Стрелка вправо\nБег                                Shift\nПрыжок                          Пробел\nРежим камеры              Tab\nИнвентарь                     I\nПерсонаж                       P\nХарактеристики             C\nКарта                             M\nЖурнал                          J\nЧат                                Enter\nМеню / закрытие          Escape";
    }
    if (window == "mapbook" && control.Id == 3)
    {
        return Bootstrap.Lang == 1 ? "World map\nCurrent region\nVisited locations" : "Карта мира\nТекущая область\nПосещённые места";
    }
    if (window == "journal_mini")
    {
        if (control.Id == 9)
        {
            if (JournalEntries.empty()) { return Bootstrap.Lang == 1 ? "No journal entries" : "В журнале пока нет записей"; }
            std::vector<std::string> lines;
            lines.reserve(JournalEntries.size());
            for (size_t index = 0; index < JournalEntries.size(); ++index)
            {
                std::string entry = JournalEntries[index];
                std::replace(entry.begin(), entry.end(), '\n', ' ');
                lines.push_back((static_cast<int32>(index) == JournalSelected ? "> " : "  ") + std::to_string(index + 1) + ". " + entry);
            }
            return JoinUiLines(lines);
        }
        if (control.Id == 8) { return GameEditText(windowName, control.Id); }
    }
    if (window == "control_options" && control.Id == 4)
    {
        return Bootstrap.Lang == 1 ? (IsGameControlChecked(window, control.Id) ? "Inverse mouse: on" : "Inverse mouse: off") : (IsGameControlChecked(window, control.Id) ? "Инверсия мыши: Вкл." : "Инверсия мыши: Выкл.");
    }
    if (window == "sound_options" && control.Id == 9)
    {
        return Bootstrap.Lang == 1 ? (IsGameControlChecked(window, control.Id) ? "HW mixing: on" : "HW mixing: off") : (IsGameControlChecked(window, control.Id) ? "Апп. микширование: Вкл." : "Апп. микширование: Выкл.");
    }
    if (window == "interface_options" && (control.Id == 7 || control.Id == 8 || control.Id == 9 || control.Id == 11 || control.Id == 12 || control.Id == 15 || control.Id == 16 || control.Id == 19 || control.Id == 21))
    {
        return Bootstrap.Lang == 1 ? (IsGameControlChecked(window, control.Id) ? "On" : "Off") : (IsGameControlChecked(window, control.Id) ? "Вкл." : "Выкл.");
    }
    if (window == "gfx_options")
    {
        auto option = [&](int32 id)
        {
            auto it = GameControlValues.find(GameUiKey(window, id));
            if (it != GameControlValues.end()) { return static_cast<int32>(std::round(it->second)); }
            if (id == 15) { return 2; }
            if (id == 17 || id == 34 || id == 39 || id == 51) { return 1; }
            return 2;
        };
        auto quality = [&](int32 value) { return Bootstrap.Lang == 1 ? (value <= 0 ? "Off" : value == 1 ? "Low" : "High") : (value <= 0 ? "Выкл." : value == 1 ? "Низкое" : "Высокое"); };
        if (control.Id == 7) { static constexpr std::array<const char*, 5> values{"640x480", "800x600", "1024x768", "1280x720", "1920x1080"}; return values[static_cast<size_t>(std::clamp(option(15), 0, 4))]; }
        if (control.Id == 8 || control.Id == 9) { return option(17) == 0 ? "16 bit" : "32 bit"; }
        if (control.Id == 10) { return quality(option(18)); }
        if (control.Id == 22) { return quality(option(24)); }
        if (control.Id == 31) { return quality(option(26)); }
        if (control.Id == 35) { return Bootstrap.Lang == 1 ? (option(34) == 0 ? "Windowed" : "Fullscreen") : (option(34) == 0 ? "Оконный" : "Полный экран"); }
        if (control.Id == 37) { return Bootstrap.Lang == 1 ? (option(39) == 0 ? "Off" : "On") : (option(39) == 0 ? "Выкл." : "Вкл."); }
        if (control.Id == 41) { return quality(option(43)); }
        if (control.Id == 49) { return Bootstrap.Lang == 1 ? (option(51) == 0 ? "Off" : "On") : (option(51) == 0 ? "Выкл." : "Вкл."); }
    }
    if (control.Class == EUiControlClass::SpinButton) { return std::to_string(static_cast<int32>(std::round(GameControlValue(windowName, control)))); }
    return control.TextKey.empty() ? std::string{} : ResolveText(control.TextKey);
}

std::string_view FUiRuntime::GameControlImage(std::string_view windowName, const FUiControlDef& control) const
{
    if ((Common::EqualsNoCase(windowName, "minimap") || Common::EqualsNoCase(windowName, "bigmap") || Common::EqualsNoCase(windowName, "new_bigmap")) && control.Id == 1) { return ActiveMapSprite; }
    return control.ImageName;
}

void FUiRuntime::SetMapDescriptor(std::string spriteName, const std::array<int32, 8>& projection, bool hasProjection)
{
    if (!spriteName.empty()) { ActiveMapSprite = std::move(spriteName); }
    MapProjection = projection;
    HasMapProjection = hasProjection;
}

void FUiRuntime::SetMapPlayerPosition(double x, double z)
{
    MapPlayerX = x;
    MapPlayerZ = z;
    HasMapPlayerPosition = std::isfinite(x) && std::isfinite(z);
}

void FUiRuntime::ClearMapPlayerPosition()
{
    MapPlayerX = 0.0;
    MapPlayerZ = 0.0;
    HasMapPlayerPosition = false;
}

std::optional<std::pair<float, float>> FUiRuntime::GameMapPlayerUv() const
{
    if (!HasMapPlayerPosition) { return std::nullopt; }
    if (!HasMapProjection) { return std::pair<float, float>{0.5f, 0.5f}; }
    const double width = static_cast<double>(MapProjection[2]) - MapProjection[0];
    const double height = static_cast<double>(MapProjection[1]) - MapProjection[3];
    if (width <= 0.0 || height <= 0.0) { return std::pair<float, float>{0.5f, 0.5f}; }
    constexpr double mapCanvasSize = 384.0;
    const double mapX = MapProjection[4] + (static_cast<double>(MapProjection[6]) - MapProjection[4]) * ((MapPlayerX - MapProjection[0]) / width);
    const double mapY = MapProjection[5] + (static_cast<double>(MapProjection[7]) - MapProjection[5]) * ((MapProjection[1] - MapPlayerZ) / height);
    return std::pair<float, float>{std::clamp(static_cast<float>(mapX / mapCanvasSize), 0.0f, 1.0f), std::clamp(static_cast<float>(mapY / mapCanvasSize), 0.0f, 1.0f)};
}

bool FUiRuntime::IsMapPlayerControl(std::string_view windowName, int32 controlId) const
{
    return (Common::EqualsNoCase(windowName, "minimap") && controlId == 10) || (Common::EqualsNoCase(windowName, "bigmap") && controlId == 19) || (Common::EqualsNoCase(windowName, "new_bigmap") && controlId == 58);
}

bool FUiRuntime::UsesRuntimeVisibility(std::string_view windowName, int32 controlId) const
{
    if (Common::EqualsNoCase(windowName, "chat_st2"))
    {
        static constexpr std::array<int32, 5> modeControls{6, 7, 21, 9, 20};
        return std::find(modeControls.begin(), modeControls.end(), controlId) != modeControls.end();
    }
    return IsMapPlayerControl(windowName, controlId);
}

bool FUiRuntime::OverridesStaticDisabled(std::string_view windowName, int32 controlId) const
{
    return (Common::EqualsNoCase(windowName, "statinfo") || Common::EqualsNoCase(windowName, "statinfo_n")) && controlId >= 27 && controlId <= 34;
}

bool FUiRuntime::IsGameControlHidden(std::string_view windowName, int32 controlId) const
{
    auto overrideIt = GameControlHidden.find(GameUiKey(windowName, controlId));
    if (overrideIt != GameControlHidden.end()) { return overrideIt->second; }
    if (Common::EqualsNoCase(windowName, "system_right") && (controlId == 11 || controlId == 12)) { return ClockDigital; }
    if (Common::EqualsNoCase(windowName, "system_right") && controlId == 3) { return !ClockDigital; }
    if (Common::EqualsNoCase(windowName, "chat_st2"))
    {
        static constexpr std::array<int32, 5> modeControls{6, 7, 21, 9, 20};
        auto it = std::find(modeControls.begin(), modeControls.end(), controlId);
        if (it != modeControls.end()) { return static_cast<int32>(std::distance(modeControls.begin(), it)) != GameChatMode; }
    }
    if ((Common::EqualsNoCase(windowName, "statinfo") || Common::EqualsNoCase(windowName, "statinfo_n")) && (controlId == 25 || controlId == 26)) { return !HasPendingStatAllocation(); }
    if (IsMapPlayerControl(windowName, controlId)) { return !HasMapPlayerPosition; }
    if (Common::EqualsNoCase(windowName, "journal_mini"))
    {
        if (controlId >= 2 && controlId <= 4) { return JournalEditing; }
        if (controlId >= 5 && controlId <= 8) { return !JournalEditing; }
        if (controlId == 9) { return JournalEditing; }
    }
    return false;
}

bool FUiRuntime::IsGameControlDisabled(std::string_view windowName, int32 controlId) const
{
    auto it = GameControlDisabled.find(GameUiKey(windowName, controlId));
    if (it != GameControlDisabled.end()) { return it->second; }
    if ((Common::EqualsNoCase(windowName, "statinfo") || Common::EqualsNoCase(windowName, "statinfo_n")) && controlId >= 27 && controlId <= 34)
    {
        const int32 statIndex = controlId - 27;
        const int32 groupStart = statIndex < 4 ? 0 : 4;
        const int32 basePool = statIndex < 4 ? CharacterRuntime.SelectedSlotInfo().TitleStats : CharacterRuntime.SelectedSlotInfo().DegreeStats;
        const int32 used = std::accumulate(StatAllocationDelta.begin() + groupStart, StatAllocationDelta.begin() + groupStart + 4, 0);
        return basePool <= 0 && StatAllocationDelta[static_cast<size_t>(statIndex)] <= 0 && used <= 0;
    }
    if (Common::EqualsNoCase(windowName, "journal_mini") && (controlId == 3 || controlId == 4)) { return JournalEntries.empty() || JournalSelected < 0; }
    return false;
}

float FUiRuntime::GameControlValue(std::string_view window, const FUiControlDef& control) const
{
    auto it = GameControlValues.find(GameUiKey(window, control.Id));
    if (it != GameControlValues.end()) { return it->second; }
    if (Common::EqualsNoCase(window, "sound_options") && control.Id == 7) { return MusicOptionVolume * 100.0f; }
    if (Common::EqualsNoCase(window, "sound_options") && control.Id == 8) { return SoundOptionVolume * 100.0f; }
    if (Common::EqualsNoCase(window, "gfx_options"))
    {
        if (control.Id == 15) { return 2.0f; }
        if (control.Id == 17 || control.Id == 34 || control.Id == 39 || control.Id == 51) { return 1.0f; }
        if (control.Id == 18 || control.Id == 24 || control.Id == 26 || control.Id == 43) { return 2.0f; }
        if (control.Id == 28) { return 65.0f; }
        if (control.Id == 46) { return 60.0f; }
    }
    return static_cast<float>(std::clamp(control.ProgressPos, control.RangeMin, control.RangeMax));
}

std::pair<float, float> FUiRuntime::AudioOptionVolumes() const
{
    return {SoundOptionVolume, MusicOptionVolume};
}

void FUiRuntime::SetGameControlValue(std::string_view window, const FUiControlDef& control, float value)
{
    const float clamped = std::clamp(value, static_cast<float>(control.RangeMin), static_cast<float>(control.RangeMax));
    GameControlValues[GameUiKey(window, control.Id)] = clamped;
    if (!Common::EqualsNoCase(window, "sound_options")) { return; }
    if (control.Id == 7) { MusicOptionVolume = std::clamp(clamped * 0.01f, 0.0f, 1.0f); }
    else if (control.Id == 8) { SoundOptionVolume = std::clamp(clamped * 0.01f, 0.0f, 1.0f); }
}

void FUiRuntime::AdjustGameControlValue(std::string_view window, const FUiControlDef& control, int32 direction)
{
    if ((Common::EqualsNoCase(window, "statinfo") || Common::EqualsNoCase(window, "statinfo_n")) && control.Id >= 27 && control.Id <= 34)
    {
        const int32 index = control.Id - 27;
        const int32 groupStart = index < 4 ? 0 : 4;
        const int32 pool = index < 4 ? CharacterRuntime.SelectedSlotInfo().TitleStats : CharacterRuntime.SelectedSlotInfo().DegreeStats;
        const int32 used = std::accumulate(StatAllocationDelta.begin() + groupStart, StatAllocationDelta.begin() + groupStart + 4, 0);
        int32& delta = StatAllocationDelta[static_cast<size_t>(index)];
        direction = -direction; // The legacy statinfo atlas labels the visible arrows opposite to their logical sub-button names.
        if (direction < 0) { delta = std::max(0, delta - 1); }
        else if (used < pool) { ++delta; }
        return;
    }
    int32 optionCount = 0;
    if (Common::EqualsNoCase(window, "gfx_options"))
    {
        if (control.Id == 15) { optionCount = 5; }
        else if (control.Id == 17 || control.Id == 34 || control.Id == 39 || control.Id == 51) { optionCount = 2; }
        else if (control.Id == 18 || control.Id == 24 || control.Id == 26 || control.Id == 43) { optionCount = 3; }
    }
    if (optionCount > 0)
    {
        const int32 current = static_cast<int32>(std::round(GameControlValue(window, control)));
        const int32 next = (current + (direction < 0 ? -1 : 1) + optionCount) % optionCount;
        GameControlValues[GameUiKey(window, control.Id)] = static_cast<float>(next);
        return;
    }
    const float step = static_cast<float>(std::max(1, control.DeltaStep));
    SetGameControlValue(window, control, GameControlValue(window, control) + step * static_cast<float>(direction));
}

float FUiRuntime::GameProgressRatio(std::string_view windowName, int32 controlId) const
{
    if (!Common::EqualsNoCase(windowName, "statinfo") && !Common::EqualsNoCase(windowName, "statinfo_n")) { return 1.0f; }
    const auto& slot = CharacterRuntime.SelectedSlotInfo();
    auto ratio = [](int32 value, int32 maximum) { return maximum > 0 ? std::clamp(static_cast<float>(value) / static_cast<float>(maximum), 0.0f, 1.0f) : 0.0f; };
    if (controlId == 3) { return ratio(slot.TitleXp, slot.TitleNextXp); }
    if (controlId == 4) { return ratio(slot.DegreeXp, slot.DegreeNextXp); }
    if (controlId == 5) { return ratio(slot.CurrentHp, slot.MaxHp); }
    if (controlId == 6) { return ratio(slot.CurrentMp, slot.MaxMp); }
    if (controlId == 7) { return ratio(slot.CurrentSatiety, slot.MaxSatiety); }
    return 1.0f;
}

float FUiRuntime::GameControlRotation(std::string_view windowName, const FUiControlDef& control) const
{
    if (Common::EqualsNoCase(windowName, "system_left") && control.Id == 11) { return CompassHeadingRadians * 57.2957795f; }
    if (!Common::EqualsNoCase(windowName, "system_right") || !HasServerGameTime) { return control.RotationDegrees; }
    const float seconds = ServerGameTime * 86400.0f;
    const float hours = std::fmod(seconds / 3600.0f, 12.0f);
    const float minutes = std::fmod(seconds / 60.0f, 60.0f);
    if (control.Id == 11) { return hours * 30.0f; }
    if (control.Id == 12) { return minutes * 6.0f; }
    return control.RotationDegrees;
}

void FUiRuntime::HandleLocalGameControl(std::string_view windowName, int32 controlId)
{
    const std::string window = Common::ToLower(std::string(windowName));
    auto focusControl = [&](int32 id)
    {
        const std::optional<size_t> index = FindGameWindowIndex(window);
        Actions.FocusedWindowIndex = index ? static_cast<int32>(*index) : -1;
        Actions.FocusedControlId = id;
    };
    if (window == "chat_st2" && controlId == 2) { CycleGameChatMode(); return; }
    if ((window == "statinfo" || window == "statinfo_n") && controlId == 26) { ResetPendingStatAllocation(); return; }
    if (window == "system_right" && controlId == 14) { ClockDigital = !ClockDigital; return; }
    if (window == "journal_mini")
    {
        if (controlId == 2) { JournalSelected = -1; JournalEditing = true; GameEditValues[GameUiKey(window, 8)].clear(); focusControl(8); return; }
        if (controlId == 3 && JournalSelected >= 0 && JournalSelected < static_cast<int32>(JournalEntries.size())) { JournalEditing = true; GameEditValues[GameUiKey(window, 8)] = JournalEntries[static_cast<size_t>(JournalSelected)]; focusControl(8); return; }
        if (controlId == 4 && JournalSelected >= 0 && JournalSelected < static_cast<int32>(JournalEntries.size())) { JournalEntries.erase(JournalEntries.begin() + JournalSelected); JournalSelected = JournalEntries.empty() ? -1 : std::min(JournalSelected, static_cast<int32>(JournalEntries.size()) - 1); return; }
        if (controlId == 5)
        {
            const std::string text = GameEditValues[GameUiKey(window, 8)];
            if (!text.empty())
            {
                if (JournalSelected >= 0 && JournalSelected < static_cast<int32>(JournalEntries.size())) { JournalEntries[static_cast<size_t>(JournalSelected)] = text; }
                else { JournalEntries.push_back(text); JournalSelected = static_cast<int32>(JournalEntries.size()) - 1; }
            }
            JournalEditing = false;
            Actions.FocusedWindowIndex = -1;
            Actions.FocusedControlId = 0;
            return;
        }
        if (controlId == 6) { JournalEditing = false; Actions.FocusedWindowIndex = -1; Actions.FocusedControlId = 0; return; }
    }
    if ((window == "gfx_options" || window == "sound_options" || window == "control_options" || window == "interface_options" || window == "font_options") && (controlId == 1 || controlId == 2)) { SetGameWindowVisible(window, false); return; }
    if (window == "control_options" && controlId == 4) { GameControlChecks[GameUiKey(window, controlId)] = !IsGameControlChecked(window, controlId); return; }
    if (window == "control_options" && controlId == 6) { GameControlChecks[GameUiKey(window, 4)] = false; return; }
    if (window == "sound_options" && controlId == 9) { GameControlChecks[GameUiKey(window, controlId)] = !IsGameControlChecked(window, controlId); return; }
    if (window == "interface_options" && (controlId == 7 || controlId == 8 || controlId == 9 || controlId == 11 || controlId == 12 || controlId == 15 || controlId == 16 || controlId == 19 || controlId == 21)) { GameControlChecks[GameUiKey(window, controlId)] = !IsGameControlChecked(window, controlId); return; }
    if (window == "interface_options" && controlId == 6)
    {
        for (int32 id : std::array<int32, 9>{7, 8, 9, 11, 12, 15, 16, 19, 21}) { GameControlChecks[GameUiKey(window, id)] = true; }
        return;
    }
    if (window == "interface_options" && controlId == 22) { SetGameWindowVisible("font_options", true); return; }
    if (window == "mapbook" && controlId == 3) { SetGameWindowVisible("bigmap", true); }
}

bool FUiRuntime::HasPendingStatAllocation() const
{
    return std::any_of(StatAllocationDelta.begin(), StatAllocationDelta.end(), [](int32 value) { return value != 0; });
}

void FUiRuntime::CommitPendingStatAllocation()
{
    FCharacterSlotInfo& slot = CharacterRuntime.MutableSelectedSlotInfo();
    std::array<int32*, 8> stats{&slot.Strength, &slot.Dexterity, &slot.Accuracy, &slot.Endurance, &slot.Fire, &slot.Water, &slot.Earth, &slot.Air};
    const int32 titleSpent = std::accumulate(StatAllocationDelta.begin(), StatAllocationDelta.begin() + 4, 0);
    const int32 degreeSpent = std::accumulate(StatAllocationDelta.begin() + 4, StatAllocationDelta.end(), 0);
    for (size_t index = 0; index < stats.size(); ++index) { *stats[index] += StatAllocationDelta[index]; }
    slot.TitleStats = std::max(0, slot.TitleStats - titleSpent);
    slot.DegreeStats = std::max(0, slot.DegreeStats - degreeSpent);
    ResetPendingStatAllocation();
}

void FUiRuntime::ResetPendingStatAllocation()
{
    StatAllocationDelta.fill(0);
}


void FUiRuntime::SelectJournalEntry(int32 index)
{
    JournalSelected = JournalEntries.empty() ? -1 : std::clamp(index, 0, static_cast<int32>(JournalEntries.size()) - 1);
}
