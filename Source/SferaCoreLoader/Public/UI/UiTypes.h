#pragma once
#include "Core/Types.h"

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

enum class EUiPopupEffect
{
    None = 0,
    AlphaIn = 1,
    AlphaOut = 2,
    MoveLeft = 3,
    MoveRight = 4,
    MoveTop = 5,
    MoveBottom = 6
};

struct FUiPopupAnimationDesc
{
    EUiPopupEffect Effect = EUiPopupEffect::None;
    float Duration = 0.20f;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    bool IsValid() const { return Effect != EUiPopupEffect::None && Duration > 0.0f; }
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
    FUiPopupAnimationDesc ShowEffect;
    FUiPopupAnimationDesc HideEffect;
    std::vector<FUiControlDef> Controls;
    std::unordered_map<std::string, FUiSpriteDef> Sprites;
};
