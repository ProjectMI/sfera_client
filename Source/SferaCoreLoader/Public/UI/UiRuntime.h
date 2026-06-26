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

class FUiRuntime 
{
public:
    FUiRuntime();
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
    void ShowCreateConfirmation();
    void ShowDeleteConfirmation();
    void DismissModal();
    const std::string& LoginBackgroundTexture() const { return Bootstrap.LoginBackgroundTexture; }
    const std::string& Stage() const { return CurrentStage; }
    const std::vector<std::string>& StatusLines() const { return Status; }
    const std::string& GameChatDraft() const { return GameChat; }
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
    void ResetModalAnimation();
    void ClearModalState();
    const FUiPopupAnimationDesc& ActiveModalAnimation() const;
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
    bool Ready = false;
};
