#pragma once
#include "Core/Logger.h"
#include "ResourceLoader/ResourceManager.h"
#include "Platform/Win32Window.h"
#include "UI/UiResourceDocument.h"
#include <string>
#include <vector>

struct HDC__;
struct tagRECT;

namespace Sfera {
struct FLoadingResourceRef {
    std::string LogicalName;
    std::string Kind;
    size_t Size = 0;
    bool Loaded = false;
};

struct FUiInteractionState {
    int32 HoverControlId = 0;
    int32 PressedControlId = 0;
    int32 FocusedControlId = 7;
    bool SaveLogin = true;
    std::string LoginText = "a1b2c3d4";
    std::string PasswordText;
    std::string LastAction;
};

class FLoadingScreenModel {
public:
    FStatus Initialize(const FResourceManager& resources, FLogger* logger);
    void SetStage(std::string stage, float progress);
    void AddStatusLine(std::string line);
    const std::string& Stage() const { return CurrentStage; }
    float Progress() const { return CurrentProgress; }
    const std::vector<std::string>& Lines() const { return StatusLines; }
    const std::vector<FLoadingResourceRef>& Resources() const { return ResourceRefs; }
    bool HasUiFile() const { return LoadedUiFile; }
    bool HasParsedLayout() const { return ParsedUiLayout; }
    const FUiWindow* LayoutWindow() const;
    const FUiWindow* ConnectionWindow() const;
    const FUiDocument* ConnectionDocument() const { return HasConnectionUiLayout ? &ConnectionUiDocument : nullptr; }
    const FUiDocumentStats& UiStats() const { return LoadedUiStats; }
    const FUiInteractionState& Interaction() const { return InteractionState; }
    bool HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger = nullptr);
private:
    static std::vector<std::string> BuildCandidateNames();
    static std::vector<std::string> DeduplicateCandidates(const FResourceManager& resources, const std::vector<std::string>& candidates);
    std::string CurrentStage = "bootstrap";
    float CurrentProgress = 0.0f;
    bool LoadedUiFile = false;
    std::vector<std::string> StatusLines;
    std::vector<FLoadingResourceRef> ResourceRefs;
    bool ParsedUiLayout = false;
    FUiDocument LoadedUiDocument;
    FUiDocumentStats LoadedUiStats;
    bool HasConnectionUiLayout = false;
    FUiDocument ConnectionUiDocument;
    FUiDocumentStats ConnectionUiStats;
    FUiInteractionState InteractionState;
};

class FLoadingScreenPainter {
public:
    explicit FLoadingScreenPainter(const FLoadingScreenModel& model);
    void Paint(HDC__* dc, const tagRECT& rect) const;
    void PaintConnectionTextOverlay(HDC__* dc, const tagRECT& rect) const;
private:
    const FLoadingScreenModel& Model;
};
}
