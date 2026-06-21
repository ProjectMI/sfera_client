#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "Platform/Win32Window.h"
#include "ResourceLoader/ResourceManager.h"
#include "Network/LoginClient.h"
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct tagRECT;

struct FUiPoint 
{ 
    int32 X = 0;
    int32 Y = 0; 
};

struct FUiSize 
{ 
    int32 W = 0;
    int32 H = 0;
};

struct FUiRect 
{ 
    int32 X = 0;
    int32 Y = 0;
    int32 W = 0;
    int32 H = 0; 
};

struct FUiRectF 
{ 
    float X = 0.0f; 
    float Y = 0.0f; 
    float W = 0.0f; 
    float H = 0.0f; 
};

struct FUiTexCoord 
{ 
    int32 U = 0; 
    int32 V = 0; 
};

struct FUiColor 
{
    int32 R = 255;
    int32 G = 255;
    int32 B = 255;
    int32 A = 255;
};

struct FUiSpritePiece 
{
    std::string TextureName;
    int32 SrcLeft = 0;
    int32 SrcTop = 0;
    int32 SrcRight = 0;
    int32 SrcBottom = 0;
    int32 DstLeft = 0;
    int32 DstTop = 0;
    int32 DstRight = 0;
    int32 DstBottom = 0;
    bool HasTexCoords = false;
    std::array<FUiTexCoord, 4> TexCoords{};
};

struct FUiSpriteDef
{
    std::string Name;
    int32 Width = 0;
    int32 Height = 0;
    std::vector<FUiSpritePiece> Pieces;
};

struct FUiSubButtonDef
{
    int32 X = 0;
    int32 Y = 0;
    int32 W = 0;
    int32 H = 0;
    std::string CheckedImage;
    std::string FocusedImage;
    std::string DisabledImage;
    std::string UncheckedImage;
};

struct FUiControlDef
{
    int32 Id = 0;
    std::string ClassId;
    FUiRect Rect;
    int32 Font = 0;
    FUiColor TextColor;
    FUiColor DisabledColor{128, 128, 128, 255};
    FUiColor FocusColor{255, 239, 212, 255};
    std::string TextKey;
    std::string CheckedImage;
    std::string UncheckedImage;
    std::string FocusedImage;
    std::string ImageName;
    std::string DrawSpriteName;
    std::string WindowHelp;
    std::string SlotEmptyImage;
    std::string SlotFullImage;
    std::string SlotBorderImage;
    std::string ScrollSpriteName;
    int32 ScrollSpriteWidth = 0;
    int32 ScrollSpriteHeight = 0;
    int32 DeltaStep = 1;
    int32 RangeMin = 0;
    int32 RangeMax = 100;
    int32 ProgressPos = 0;
    std::string StatusShow;
    FUiPoint StatusPos;
    FUiSubButtonDef LeftButton;
    FUiSubButtonDef RightButton;
    bool Password = false;
    bool SendQuit = false;
    bool SendHelp = false;
    bool Hidden = false;
    bool Disabled = false;
    bool TextCenter = false;
    int32 MaxSymbols = 0;
    bool EnteredOnFocus = false;
    std::string HotKey;
    std::string HyperTextResource;
    FUiPoint ImageOffset;
    int32 Group = 0;
};

struct FUiWindowDef 
{
    std::string Name;
    std::string TextKey;
    FUiRect Rect;
    int32 Font = 0;
    FUiColor TextColor{237, 208, 161, 255};
    FUiRect TitleRect;
    bool AlignCenterX = false;
    bool AlignCenterY = false;
    bool AlignRightX = false;
    bool AlignRightY = false;
    bool SaveLastPosition = false;
    bool CanDragDrop = false;
    bool CanNotCross = false;
    bool CanGoTop = true;
    bool EscapeHandle = false;
    bool DrawNone = false;
    std::string DrawSpriteName;
    std::vector<FUiControlDef> Controls;
    std::unordered_map<std::string, FUiSpriteDef> Sprites;
};

using FUiStringTable = std::unordered_map<std::string, std::string>;

struct FUiActionState 
{
    int32 HoverControlId = 0;
    int32 PressedControlId = 0;
    int32 FocusedControlId = 7;
    int32 LastControlId = 0;
    bool SaveLogin = true;
    std::string LoginText;
    std::string PasswordText;
    std::string LastAction;
    int32 SpinHoverDirection = 0;
    int32 SpinPressedDirection = 0;
};

enum class EUiRuntimeMode 
{
    Login,
    CharacterSelect,
    Game 
};

enum class EUiModalDialog 
{ 
    None, 
    CharacterExit, 
    CharacterCreate,
    CharacterDelete 
};

struct FUiBootstrapDesc
{
    std::string StringsResource = "language/strings.ui";
    std::string ConnectionWindowResource = "effects/connection.ui";
    std::string PickPersonWindowResource = "effects/pickpers.ui";
    std::string CreatePersonWindowResource = "effects/createdude.ui";
    std::string DeleteCharacterWindowResource = "effects/delete_character.ui";
    std::string ConnectMessageWindowResource = "effects/connect_message.ui";
    std::string MessageWindowResource = "effects/message.ui";
    std::string LoginBackgroundTexture = "xadd/login_rus_sp.dds";
    int32 DesignWidth = 1024;
    int32 DesignHeight = 768;
    int32 Lang = 0;
};

struct FCharacterUiAppearance 
{
    int32 Gender = 0;
    int32 Face = 0;
    int32 Hair = 0;
    int32 HairColor = 0;
    int32 Tattoo = 0;
    int32 Strength = 0;
    int32 Dexterity = 0;
    int32 Accuracy = 0;
    int32 Endurance = 0;
    int32 Fire = 0;
    int32 Water = 0;
    int32 Earth = 0;
    int32 Air = 0;
};

class FUiRuntime 
{
public:
    FStatus Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger = nullptr);
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    bool HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger = nullptr);
    FUiRectF BuildDesignRect(const tagRECT& clientRect) const;
    FUiRectF BuildConnectionRect(const tagRECT& clientRect) const;
    FUiRectF BuildWindowRect(const FUiWindowDef& window, const tagRECT& clientRect) const;
    const FUiStringTable& Strings() const { return StringTable; }
    const FUiWindowDef& ConnectionWindow() const { return Connection; }
    const FUiWindowDef& PickPersonWindow() const { return PickPerson; }
    const FUiWindowDef& CreatePersonWindow() const { return CreatePerson; }
    const FUiWindowDef& DeleteCharacterWindow() const { return DeleteCharacter; }
    const FUiWindowDef& ConnectMessageWindow() const { return ConnectMessage; }
    const FUiWindowDef& MessageWindow() const { return Message; }
    const FUiWindowDef& ActiveModalWindow() const;
    EUiModalDialog ModalDialog() const { return Modal; }
    bool HasModalDialog() const { return Modal != EUiModalDialog::None; }
    const std::string& ModalMessage() const { return ModalText; }
    const FUiActionState& ActionState() const { return Actions; }
    std::string ConsumeLastAction();
    void SetMode(EUiRuntimeMode mode);
    EUiRuntimeMode Mode() const { return CurrentMode; }
    void SetCharacterSlots(const std::array<FCharacterSlotInfo, 3>& slots);
    void SetCharacterAppearanceRules(const FCharacterAppearanceRules& rules);
    void SetCharacterActionLocked(bool locked);
    void SetLoginCredentials(std::string login, std::string password, bool saveLogin);
    void ShowExitConfirmation();
    void ShowCreateConfirmation();
    void ShowDeleteConfirmation();
    void DismissModal();
    void ApplyCharacterDeleted(int32 slot);
    void ApplyCharacterCreated(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance);
    const std::array<FCharacterSlotInfo, 3>& CharacterSlots() const { return CharacterSlotState; }
    int32 SelectedCharacterSlot() const { return SelectedSlot; }
    bool SelectedCharacterPresent() const;
    bool SelectedCharacterCanCreate() const;
    std::wstring SelectedCharacterName() const;
    FCharacterCreationAppearance SelectedCharacterAppearance(const FCharacterAppearanceRules& rules) const;
    FCharacterCreationAppearance SelectedCharacterSceneAppearance() const;
    float CharacterSceneAngle() const { return SceneAngle; }
    int32 CharacterCameraFocusId() const { return SceneCameraFocusId; }
    std::string CharacterControlText(const FUiControlDef& control) const;
    std::string CharacterProgressText(const FUiControlDef& control) const;
    std::string ModalControlText(const FUiControlDef& control) const;
    bool IsModalActionAllowed(const FUiControlDef& control) const;
    float CharacterProgressRatio(int32 controlId) const;
    const std::string& LoginBackgroundTexture() const { return Bootstrap.LoginBackgroundTexture; }
    const std::string& Stage() const { return CurrentStage; }
    const std::vector<std::string>& StatusLines() const { return Status; }
    const std::string& GameChatDraft() const { return GameChat; }
    float Progress() const { return CurrentProgress; }
    int32 DesignWidth() const { return Bootstrap.DesignWidth; }
    int32 DesignHeight() const { return Bootstrap.DesignHeight; }
    bool IsReady() const { return Ready; }
    std::string ResolveText(std::string_view key) const;
private:
    const FUiControlDef* HitTestConnection(int32 x, int32 y, const tagRECT& clientRect) const;
    const FUiControlDef* HitTestCharacterSelect(int32 x, int32 y, const tagRECT& clientRect) const;
    const FUiControlDef* HitTestModal(int32 x, int32 y, const tagRECT& clientRect) const;
    bool IsEditControl(const FUiControlDef& control) const;
    bool IsCheckControl(const FUiControlDef& control) const;
    bool IsButtonControl(const FUiControlDef& control) const;
    void ActivateControl(const FUiControlDef& control, FLogger* logger);
    void ActivateCharacterControl(const FUiControlDef& control, FLogger* logger);
    void ActivateModalControl(const FUiControlDef& control, FLogger* logger);
    FUiControlDef* MutableCharacterControlById(int32 id);
    const FUiControlDef* CharacterControlById(int32 id) const;
    void SyncCharacterSelectControls();
    void ClampCharacterAppearance();
    int32 CharacterAppearanceOptionCount(int32 controlId) const;
    int32 CharacterFocusForControl(int32 controlId) const;
    int32 CharacterSpinDeltaForPoint(const FUiControlDef& control, int32 x, int32 y, const tagRECT& clientRect) const;
    bool PointInsidePickPersonWindow(int32 x, int32 y, const tagRECT& clientRect) const;
    std::wstring DefaultCharacterNameForSlot(int32 slot) const;
    std::string EmptyCharacterSlotText() const;
    std::string CharacterGenderText(bool female) const;
    bool ModalEditMatchesSelectedCharacter() const;
    int32 CharacterTextValue(int32 id) const;
    std::string CharacterTitleText() const;
    std::string CharacterDegreeText() const;
    std::string CharacterKarmaText() const;
    FUiBootstrapDesc Bootstrap;
    FUiStringTable StringTable;
    FUiWindowDef Connection;
    FUiWindowDef PickPerson;
    FUiWindowDef CreatePerson;
    FUiWindowDef DeleteCharacter;
    FUiWindowDef ConnectMessage;
    FUiWindowDef Message;
    FUiActionState Actions;
    EUiRuntimeMode CurrentMode = EUiRuntimeMode::Login;
    EUiModalDialog Modal = EUiModalDialog::None;
    std::string ModalText;
    std::string ModalEditText;
    std::array<FCharacterSlotInfo, 3> CharacterSlotState{};
    int32 SelectedSlot = 0;
    int32 ActiveCharacterEditId = 60;
    int32 CharacterSpinDelta = 1;
    int32 SceneCameraFocusId = 0;
    float SceneAngle = 3.1415926535f;
    bool SceneRotateDragActive = false;
    int32 SceneRotateLastX = 0;
    bool CharacterActionLocked = false;
    std::array<std::wstring, 3> CharacterNameEdits{};
    FCharacterAppearanceRules AppearanceRules;
    FCharacterUiAppearance Appearance;
    std::string CurrentStage = "bootstrap";
    float CurrentProgress = 0.0f;
    std::vector<std::string> Status;
    std::string GameChat;
    bool Ready = false;
};

TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName);
TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName);