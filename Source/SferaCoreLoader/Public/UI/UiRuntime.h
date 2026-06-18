#pragma once
#include "Core/Logger.h"
#include "Core/Types.h"
#include "Platform/Win32Window.h"
#include "ResourceLoader/ResourceManager.h"
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct tagRECT;

namespace Sfera {
struct FUiPoint { int32 X = 0; int32 Y = 0; };
struct FUiSize { int32 W = 0; int32 H = 0; };
struct FUiRect { int32 X = 0; int32 Y = 0; int32 W = 0; int32 H = 0; };
struct FUiRectF { float X = 0.0f; float Y = 0.0f; float W = 0.0f; float H = 0.0f; };
struct FUiTexCoord { int32 U = 0; int32 V = 0; };

struct FUiColor {
    int32 R = 255;
    int32 G = 255;
    int32 B = 255;
    int32 A = 255;
};

struct FUiSpritePiece {
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

struct FUiSpriteDef {
    std::string Name;
    int32 Width = 0;
    int32 Height = 0;
    std::vector<FUiSpritePiece> Pieces;
};

struct FUiSubButtonDef {
    int32 X = 0;
    int32 Y = 0;
    int32 W = 0;
    int32 H = 0;
    std::string CheckedImage;
    std::string FocusedImage;
    std::string DisabledImage;
};

struct FUiControlDef {
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
    FUiSubButtonDef LeftButton;
    FUiSubButtonDef RightButton;
    bool Password = false;
    bool SendQuit = false;
    bool SendHelp = false;
    bool Hidden = false;
    bool Disabled = false;
    bool TextCenter = false;
};

struct FUiWindowDef {
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

struct FUiActionState {
    int32 HoverControlId = 0;
    int32 PressedControlId = 0;
    int32 FocusedControlId = 7;
    int32 LastControlId = 0;
    bool SaveLogin = true;
    std::string LoginText;
    std::string PasswordText;
    std::string LastAction;
};

struct FUiBootstrapDesc {
    std::string StringsResource = "language/strings.ui";
    std::string ConnectionWindowResource = "effects/connection.ui";
    std::string LoginBackgroundTexture = "xadd/login_rus_sp.dds";
    int32 DesignWidth = 1024;
    int32 DesignHeight = 768;
};

class FUiRuntime {
public:
    FStatus Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger = nullptr);
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    bool HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger = nullptr);
    FUiRectF BuildDesignRect(const tagRECT& clientRect) const;
    FUiRectF BuildConnectionRect(const tagRECT& clientRect) const;
    const FUiStringTable& Strings() const { return StringTable; }
    const FUiWindowDef& ConnectionWindow() const { return Connection; }
    const FUiActionState& ActionState() const { return Actions; }
    const std::string& LoginBackgroundTexture() const { return Bootstrap.LoginBackgroundTexture; }
    const std::string& Stage() const { return CurrentStage; }
    const std::vector<std::string>& StatusLines() const { return Status; }
    float Progress() const { return CurrentProgress; }
    int32 DesignWidth() const { return Bootstrap.DesignWidth; }
    int32 DesignHeight() const { return Bootstrap.DesignHeight; }
    bool IsReady() const { return Ready; }
    std::string ResolveText(std::string_view key) const;
private:
    const FUiControlDef* HitTestConnection(int32 x, int32 y, const tagRECT& clientRect) const;
    bool IsEditControl(const FUiControlDef& control) const;
    bool IsCheckControl(const FUiControlDef& control) const;
    bool IsButtonControl(const FUiControlDef& control) const;
    void ActivateControl(const FUiControlDef& control, FLogger* logger);
    FUiBootstrapDesc Bootstrap;
    FUiStringTable StringTable;
    FUiWindowDef Connection;
    FUiActionState Actions;
    std::string CurrentStage = "bootstrap";
    float CurrentProgress = 0.0f;
    std::vector<std::string> Status;
    bool Ready = false;
};

TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName);
TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName);
}
