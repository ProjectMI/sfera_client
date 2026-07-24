#pragma once
#include "Components/UI/GameUiRoutingComponent.h"
#include "Platform/Win64Window.h"
#include "UI/UiRuntimeState.h"

class FUiRuntime;
class FLogger;

class FUiRuntimeInput
{
public:
    explicit FUiRuntimeInput(FUiRuntime& runtime);
    FUiRectF BuildDesignRect(const RECT& clientRect) const;
    FUiRectF BuildConnectionRect(const RECT& clientRect) const;
    FUiRectF BuildWindowRect(const FUiWindowDef& window, const RECT& clientRect) const;
    bool HandleInputFrame(const FInputSnapshot& input, const RECT& clientRect, FLogger* logger = nullptr);
    void SetMode(EUiRuntimeMode mode);
private:
    const FUiControlDef* HitTestConnection(int32 x, int32 y, const RECT& clientRect) const;
    const FUiControlDef* HitTestCharacterSelect(int32 x, int32 y, const RECT& clientRect) const;
    const FUiControlDef* HitTestModal(int32 x, int32 y, const RECT& clientRect) const;
    const FUiControlDef* HitTestGame(int32 x, int32 y, const RECT& clientRect, int32& windowIndex) const;
    int32 HitTestGameWindow(int32 x, int32 y, const RECT& clientRect) const;
    bool IsEditControl(const FUiControlDef& control) const;
    bool IsCheckControl(const FUiControlDef& control) const;
    bool IsButtonControl(const FUiControlDef& control) const;
    void ActivateControl(const FUiControlDef& control, FLogger* logger);
    void ActivateCharacterControl(const FUiControlDef& control, FLogger* logger);
    void ActivateModalControl(const FUiControlDef& control, FLogger* logger);
    void ActivateGameControl(int32 windowIndex, const FUiControlDef& control, FLogger* logger);
    std::string GameControlAction(const FUiWindowDef& window, const FUiControlDef& control) const;
    void HandleGameHotkeys(const FInputSnapshot& input, FLogger* logger, bool& changed);
    int32 CharacterFocusForControl(int32 controlId) const;
    int32 CharacterSpinDeltaForPoint(const FUiControlDef& control, int32 x, int32 y, const RECT& clientRect) const;
    bool PointInsidePickPersonWindow(int32 x, int32 y, const RECT& clientRect) const;
    FUiRuntime& Runtime;
    FGameUiRoutingComponent GameUiRoutes;
    int32 SliderDragWindow = -1;
    int32 SliderDragControl = 0;
};
