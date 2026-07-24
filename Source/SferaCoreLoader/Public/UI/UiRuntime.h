#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "Platform/Win64Window.h"
#include "ResourceLoader/ResourceManager.h"
#include "UI/UiConstants.h"
#include "UI/UiDocumentParser.h"
#include "UI/UiRuntimeCharacter.h"
#include "UI/UiRuntimeInput.h"
#include "UI/UiRuntimeState.h"

struct FGameChatLine
{
    std::string Text;
    FUiColor Color{255, 255, 255, 255};
};

class FUiRuntime 
{
public:
    FUiRuntime();
    ~FUiRuntime();
    FStatus Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger = nullptr);
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    void Tick(float deltaSeconds);
    const FUiStringTable& Strings() const { return StringTable; }
    const FUiWindowDef& ConnectionWindow() const { return Connection; }
    const FUiWindowDef& PickPersonWindow() const { return PickPerson; }
    const FUiWindowDef& CreatePersonWindow() const { return CreatePerson; }
    const FUiWindowDef& DeleteCharacterWindow() const { return DeleteCharacter; }
    const FUiWindowDef& ConnectMessageWindow() const { return ConnectMessage; }
    const FUiWindowDef& MessageWindow() const { return Message; }
    const std::vector<FUiWindowDef>& GameWindows() const { return GameWindowDefs; }
    const std::vector<bool>& GameWindowVisibility() const { return GameWindowVisible; }
    const std::vector<size_t>& GameWindowRenderOrder() const { return GameWindowOrder; }
    std::optional<size_t> FindGameWindowIndex(std::string_view name) const;
    bool IsGameWindowVisible(std::string_view name) const;
    bool SetGameWindowVisible(std::string_view name, bool visible);
    bool ToggleGameWindow(std::string_view name);
    void BringGameWindowToFront(size_t index);
    bool CloseTopGameWindow();
    void ResetGameWindows();
    FUiRectF BuildGameWindowRect(size_t index, const RECT& clientRect) const;
    bool IsGameControlChecked(std::string_view window, int32 controlId) const;
    const std::string& GameEditText(std::string_view window, int32 controlId) const;
    bool IsGameTextInputFocused() const;
    void ToggleGameControlChecked(std::string_view window, const FUiControlDef& control);
    const FUiWindowDef& ActiveModalWindow() const;
    EUiModalDialog ModalDialog() const { return Modal; }
    bool HasModalDialog() const { return Modal != EUiModalDialog::None; }
    bool IsModalClosing() const { return ModalClosing; }
    float ModalAnimationProgress() const;
    float ModalAnimationAlpha() const;
    FUiRectF BuildAnimatedModalRect(const RECT& clientRect) const;
    const std::string& ModalMessage() const { return ModalText; }
    const FUiActionState& ActionState() const { return Actions; }
    std::string ConsumeLastAction();
    EUiRuntimeMode Mode() const { return CurrentMode; }
    void SetLoginCredentials(std::string login, std::string password, bool saveLogin);
    void ShowExitConfirmation();
    void ShowGameExitConfirmation();
    void ShowCreateConfirmation();
    void ShowDeleteConfirmation();
    void DismissModal();
    const std::string& LoginBackgroundTexture() const { return Bootstrap.LoginBackgroundTexture; }
    const std::string& Stage() const { return CurrentStage; }
    const std::vector<std::string>& StatusLines() const { return Status; }
    const std::string& GameChatDraft() const { return GameChat; }
    bool IsGameChatFocused() const { return GameChatFocused; }
    void SetGameChatFocused(bool focused) { GameChatFocused = focused; }
    std::string TakeGameChatDraft();
    void ClearGameChatDraft() { GameChat.clear(); }
    void AppendGameChatLine(std::string line, int32 mode = -1);
    const std::vector<FGameChatLine>& GameChatHistory() const { return GameChatLines; }
    int32 GameChatModeIndex() const { return GameChatMode; }
    void CycleGameChatMode();
    uint8 GameChatChannel() const;
    FUiColor GameChatModeColor() const;
    bool IsTextCaretVisible() const;
    void SetServerGameTime(float dayFraction, int32 day = 0, int32 month = 0, int32 year = 0);
    void ClearServerGameTime();
    void SetCompassHeading(float radians) { CompassHeadingRadians = radians; }
    void SetClanAvailable(bool available) { ClanAvailable = available; }
    bool IsClanAvailable() const { return ClanAvailable; }
    void SetGroupAvailable(bool available) { GroupAvailable = available; }
    bool IsGroupAvailable() const { return GroupAvailable; }
    std::string GameControlText(std::string_view window, const FUiControlDef& control) const;
    std::string_view GameControlImage(std::string_view window, const FUiControlDef& control) const;
    void SetMapDescriptor(std::string spriteName, const std::array<int32, 8>& projection, bool hasProjection);
    void SetMapPlayerPosition(double x, double z);
    void ClearMapPlayerPosition();
    std::optional<std::pair<float, float>> GameMapPlayerUv() const;
    bool IsMapPlayerControl(std::string_view window, int32 controlId) const;
    bool UsesRuntimeVisibility(std::string_view window, int32 controlId) const;
    bool OverridesStaticDisabled(std::string_view window, int32 controlId) const;
    bool IsGameControlHidden(std::string_view window, int32 controlId) const;
    bool IsGameControlDisabled(std::string_view window, int32 controlId) const;
    float GameControlValue(std::string_view window, const FUiControlDef& control) const;
    void SetGameControlValue(std::string_view window, const FUiControlDef& control, float value);
    void AdjustGameControlValue(std::string_view window, const FUiControlDef& control, int32 direction);
    float GameProgressRatio(std::string_view window, int32 controlId) const;
    float GameControlRotation(std::string_view window, const FUiControlDef& control) const;
    void SetHelpTopic(std::string topic) { CurrentHelpTopic = std::move(topic); }
    void HandleLocalGameControl(std::string_view window, int32 controlId);
    std::array<int32, 8> PendingStatAllocation() const { return StatAllocationDelta; }
    bool HasPendingStatAllocation() const;
    void CommitPendingStatAllocation();
    void ResetPendingStatAllocation();
    int32 JournalEntryCount() const { return static_cast<int32>(JournalEntries.size()); }
    void SelectJournalEntry(int32 index);
    float Progress() const { return CurrentProgress; }
    int32 DesignWidth() const { return Bootstrap.DesignWidth; }
    int32 DesignHeight() const { return Bootstrap.DesignHeight; }
    bool IsReady() const { return Ready; }
    std::string ResolveText(std::string_view key) const;
    FUiDocumentParser& Parser() { return DocumentParser; }
    const FUiDocumentParser& Parser() const { return DocumentParser; }
    FUiRuntimeCharacter& Character() { return CharacterRuntime; }
    const FUiRuntimeCharacter& Character() const { return CharacterRuntime; }
    FUiRuntimeInput& Input() { return InputRuntime; }
    const FUiRuntimeInput& Input() const { return InputRuntime; }
private:
    friend class FUiRuntimeCharacter;
    friend class FUiRuntimeInput;
    struct FDeferredGameUiBatch
    {
        std::vector<FUiWindowDef> Windows;
        std::vector<std::string> Warnings;
    };
    void ResetModalAnimation();
    void ClearModalState();
    const FUiPopupAnimationDesc& ActiveModalAnimation() const;
    void RegisterGameWindow(FUiWindowDef window, bool initiallyVisible);
    void PumpDeferredGameWindows(bool waitForCompletion = false);
    FUiDocumentParser DocumentParser;
    FUiRuntimeCharacter CharacterRuntime;
    FUiRuntimeInput InputRuntime;
    FUiBootstrapDesc Bootstrap;
    FUiStringTable StringTable;
    FUiWindowDef Connection;
    FUiWindowDef PickPerson;
    FUiWindowDef CreatePerson;
    FUiWindowDef DeleteCharacter;
    FUiWindowDef ConnectMessage;
    FUiWindowDef Message;
    std::vector<FUiWindowDef> GameWindowDefs;
    std::vector<bool> GameWindowVisible;
    std::vector<size_t> GameWindowOrder;
    std::unordered_map<std::string, size_t, FUiStringHash, std::equal_to<>> GameWindowLookup;
    std::unordered_map<std::string, bool, FUiStringHash, std::equal_to<>> DeferredWindowVisibility;
    std::vector<FUiPoint> GameWindowPositions;
    std::vector<bool> GameWindowPositionOverrides;
    std::unordered_map<uint64, bool> GameControlChecks;
    std::unordered_map<uint64, std::string> GameEditValues;
    std::unordered_map<uint64, float> GameControlValues;
    std::unordered_map<uint64, bool> GameControlHidden;
    std::unordered_map<uint64, bool> GameControlDisabled;
    FUiActionState Actions;
    EUiRuntimeMode CurrentMode = EUiRuntimeMode::Login;
    EUiModalDialog Modal = EUiModalDialog::None;
    bool ModalClosing = false;
    float ModalAnimationTime = 0.0f;
    std::string ModalText;
    std::string ModalEditText;
    std::array<FCharacterSlotInfo, Sfera::CharacterSlotCount> CharacterSlotState{};
    int32 SelectedSlot = 0;
    int32 ActiveCharacterEditId = SferaUi::CharacterNameEditBaseId;
    int32 CharacterSpinDelta = 1;
    int32 SceneCameraFocusId = 0;
    float SceneAngle = Sfera::InitialCharacterSceneAngle;
    bool SceneRotateDragActive = false;
    int32 SceneRotateLastX = 0;
    bool CharacterActionLocked = false;
    std::array<std::wstring, Sfera::CharacterSlotCount> CharacterNameEdits{};
    FCharacterAppearanceRules AppearanceRules;
    FCharacterUiAppearance Appearance;
    std::string CurrentStage = "bootstrap";
    float CurrentProgress = 0.0f;
    std::vector<std::string> Status;
    std::string GameChat;
    std::vector<FGameChatLine> GameChatLines;
    int32 GameChatMode = 0;
    bool GameChatFocused = false;
    bool GroupAvailable = false;
    float UiAnimationTime = 0.0f;
    float ServerGameTime = 0.0f;
    bool HasServerGameTime = false;
    int32 ServerGameDay = 0;
    int32 ServerGameMonth = 0;
    int32 ServerGameYear = 0;
    bool ClockDigital = false;
    bool ClanAvailable = false;
    float CompassHeadingRadians = 0.0f;
    std::array<int32, 8> StatAllocationDelta{};
    std::string ActiveMapSprite = "mhp1";
    std::array<int32, 8> MapProjection{};
    bool HasMapProjection = false;
    double MapPlayerX = 0.0;
    double MapPlayerZ = 0.0;
    bool HasMapPlayerPosition = false;
    std::string CurrentHelpTopic;
    std::vector<std::string> JournalEntries;
    int32 JournalSelected = -1;
    bool JournalEditing = false;
    int32 GameDragWindowIndex = -1;
    int32 GameDragOffsetX = 0;
    int32 GameDragOffsetY = 0;
    std::future<FDeferredGameUiBatch> DeferredGameUiFuture;
    FLogger* DeferredGameUiLogger = nullptr;
    bool DeferredGameUiIntegrated = true;
    bool Ready = false;
};
