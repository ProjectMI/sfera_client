#pragma once
#include "Common/SferaGameConstants.h"
#include "Core/Types.h"
#include "Network/LoginClient.h"
#include "UI/UiTypes.h"

using FUiStringTable = std::unordered_map<std::string, std::string>;

struct FUiActionState 
{
    int32 HoverControlId = 0;
    int32 PressedControlId = 0;
    int32 FocusedControlId = 7;
    int32 LastControlId = 0;
    int32 HoverWindowIndex = -1;
    int32 PressedWindowIndex = -1;
    int32 FocusedWindowIndex = -1;
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
    CharacterDelete,
    GameExit
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
    int32 ChatListFont = 4;
    int32 ChatEditFont = 4;
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
