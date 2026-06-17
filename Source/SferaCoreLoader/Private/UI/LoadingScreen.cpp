#include "UI/LoadingScreen.h"
#include "FileSystem/PathUtils.h"
#include "ResourceLoader/ResourceTypes.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>

namespace Sfera {
namespace {
int ScaleX(int value, int designWidth, const tagRECT& rect) { return designWidth > 0 ? rect.left + value * (rect.right - rect.left) / designWidth : value; }
int ScaleY(int value, int designHeight, const tagRECT& rect) { return designHeight > 0 ? rect.top + value * (rect.bottom - rect.top) / designHeight : value; }
RECT ScaleRect(int x, int y, int w, int h, int designWidth, int designHeight, const tagRECT& rect) { return RECT{ScaleX(x, designWidth, rect), ScaleY(y, designHeight, rect), ScaleX(x + w, designWidth, rect), ScaleY(y + h, designHeight, rect)}; }
bool EqualsNoCase(std::string_view a, std::string_view b) { if (a.size() != b.size()) { return false; } for (size_t i = 0; i < a.size(); ++i) { if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) { return false; } } return true; }
const FUiControl* FindFirstControl(const FUiWindow& window, std::string_view classId) { for (const auto& control : window.Controls) { if (EqualsNoCase(control.ClassId, classId)) { return &control; } } return nullptr; }
}

std::vector<std::string> FLoadingScreenModel::BuildCandidateNames() { return {"Effects/loadscreen.ui", "Effects\\loadscreen.ui", "loadscreen.ui", "effects/loadscreen.ui", "Interface/loadscreen.ui", "UI/loadscreen.ui"}; }

std::vector<std::string> FLoadingScreenModel::DeduplicateCandidates(const FResourceManager& resources, const std::vector<std::string>& candidates) {
    std::vector<std::string> result;
    std::set<std::string> physical;
    std::set<std::string> logical;
    for (const auto& name : candidates) {
        auto record = resources.Catalog().FindByLogicalName(name);
        if (!record) { continue; }
        std::string relative = PathUtils::NormalizeForLookup(record->RelativePath);
        std::string requested = PathUtils::NormalizeForLookup(FPath{name});
        if (physical.insert(relative).second && logical.insert(requested).second) { result.push_back(record->RelativePath.generic_string()); }
    }
    return result;
}

FStatus FLoadingScreenModel::Initialize(const FResourceManager& resources, FLogger* logger) {
    ResourceRefs.clear();
    StatusLines.clear();
    LoadedUiFile = false;
    ParsedUiLayout = false;
    LoadedUiDocument = FUiDocument{};
    LoadedUiStats = FUiDocumentStats{};
    HasConnectionUiLayout = false;
    ConnectionUiDocument = FUiDocument{};
    ConnectionUiStats = FUiDocumentStats{};
    for (const auto& name : DeduplicateCandidates(resources, BuildCandidateNames())) {
        auto record = resources.Catalog().FindByLogicalName(name);
        if (!record) { continue; }
        auto blob = resources.Load(name);
        FLoadingResourceRef ref;
        ref.LogicalName = name;
        ref.Kind = ToString(GuessResourceKind(record->RelativePath));
        if (blob.IsOk()) {
            ref.Size = blob.Value().Bytes.size();
            ref.Loaded = true;
            LoadedUiFile = true;
            auto parsed = FUiResourceDocument::ParseText(blob.Value().Id.LogicalName, blob.Value().SourcePath.string(), blob.Value().Bytes, blob.Value().WasCompressed);
            if (parsed.IsOk()) {
                LoadedUiDocument = std::move(parsed.Value());
                LoadedUiStats = LoadedUiDocument.Stats();
                ParsedUiLayout = LoadedUiStats.WindowCount > 0;
                ref.Kind = "UI";
            } else if (logger) { logger->Warning("LoadingScreen UI parse failed: " + parsed.Status().Message()); }
        }
        ResourceRefs.push_back(ref);
        if (logger) { logger->Info("LoadingScreen resource: " + name + ", kind=" + ref.Kind + ", loaded=" + std::string(ref.Loaded ? "yes" : "no") + ", bytes=" + std::to_string(ref.Size)); }
    }
    if (ParsedUiLayout && logger) { logger->Info("LoadingScreen layout: windows=" + std::to_string(LoadedUiStats.WindowCount) + ", controls=" + std::to_string(LoadedUiStats.ControlCount) + ", sprites=" + std::to_string(LoadedUiStats.SpriteCount) + ", textures=" + std::to_string(LoadedUiStats.TextureNameCount)); }
    for (const char* connectionName : {"Effects/connection.ui", "effects/connection.ui", "connection.ui"}) {
        auto connectionBlob = resources.Load(connectionName);
        if (!connectionBlob.IsOk()) { continue; }
        auto parsedConnection = FUiResourceDocument::ParseText(connectionBlob.Value().Id.LogicalName, connectionBlob.Value().SourcePath.string(), connectionBlob.Value().Bytes, connectionBlob.Value().WasCompressed);
        if (parsedConnection.IsOk()) {
            ConnectionUiDocument = std::move(parsedConnection.Value());
            ConnectionUiStats = ConnectionUiDocument.Stats();
            HasConnectionUiLayout = ConnectionUiStats.WindowCount > 0;
            if (logger) { logger->Info("Connection UI layout: file=" + std::string(connectionName) + ", windows=" + std::to_string(ConnectionUiStats.WindowCount) + ", controls=" + std::to_string(ConnectionUiStats.ControlCount) + ", sprites=" + std::to_string(ConnectionUiStats.SpriteCount) + ", textures=" + std::to_string(ConnectionUiStats.TextureNameCount)); }
            break;
        }
        if (logger) { logger->Warning("Connection UI parse failed: " + parsedConnection.Status().Message()); }
    }
    if (!LoadedUiFile && logger) { logger->Warning("LoadingScreen ui file not found; using recovered GDI fallback, expected Effects/loadscreen.ui or loadscreen.ui"); }
    SetStage("resources loaded", 0.25f);
    return FStatus::Ok();
}

const FUiWindow* FLoadingScreenModel::LayoutWindow() const {
    if (!ParsedUiLayout) { return nullptr; }
    if (const FUiWindow* named = LoadedUiDocument.FindWindowByName("loadscreen")) { return named; }
    return LoadedUiDocument.Windows.empty() ? nullptr : &LoadedUiDocument.Windows.front();
}

const FUiWindow* FLoadingScreenModel::ConnectionWindow() const {
    if (!HasConnectionUiLayout) { return nullptr; }
    if (const FUiWindow* named = ConnectionUiDocument.FindWindowByName("connection")) { return named; }
    return ConnectionUiDocument.Windows.empty() ? nullptr : &ConnectionUiDocument.Windows.front();
}

namespace {
struct FConnectionGeometry {
    double Scale = 1.0;
    int OriginX = 0;
    int OriginY = 0;
};

FConnectionGeometry BuildConnectionGeometry(const FUiWindow& load, const FUiWindow& connection, const tagRECT& rect) {
    int designWidth = load.Size.X > 0 ? load.Size.X : 1024;
    int designHeight = load.Size.Y > 0 ? load.Size.Y : 768;
    double scale = std::min((rect.right - rect.left) / static_cast<double>(designWidth), (rect.bottom - rect.top) / static_cast<double>(designHeight));
    int ox = static_cast<int>((rect.right - rect.left - designWidth * scale) * 0.5);
    int oy = static_cast<int>((rect.bottom - rect.top - designHeight * scale) * 0.5);
    FConnectionGeometry out;
    out.Scale = scale;
    out.OriginX = ox + static_cast<int>((designWidth - connection.Size.X) * 0.5 * scale);
    out.OriginY = oy + static_cast<int>((designHeight - connection.Size.Y) * 0.5 * scale - 10.0 * scale);
    return out;
}

const FUiControl* HitTestConnectionControl(const FUiWindow& connection, const FConnectionGeometry& g, int x, int y) {
    for (auto it = connection.Controls.rbegin(); it != connection.Controls.rend(); ++it) {
        const FUiControl& control = *it;
        if (control.Hidden || control.Disabled || control.Size.X <= 0 || control.Size.Y <= 0) { continue; }
        int left = g.OriginX + static_cast<int>(control.Position.X * g.Scale);
        int top = g.OriginY + static_cast<int>(control.Position.Y * g.Scale);
        int right = left + static_cast<int>(control.Size.X * g.Scale);
        int bottom = top + static_cast<int>(control.Size.Y * g.Scale);
        if (x >= left && x < right && y >= top && y < bottom) { return &control; }
    }
    return nullptr;
}

bool IsEditControlId(int32 id) { return id == 7 || id == 8; }
bool IsCheckControlId(int32 id) { return id == 9 || id == 10; }
}

bool FLoadingScreenModel::HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger) {
    const FUiWindow* load = LayoutWindow();
    const FUiWindow* connection = ConnectionWindow();
    if (!load || !connection) { return false; }
    FConnectionGeometry geometry = BuildConnectionGeometry(*load, *connection, clientRect);
    const FUiControl* hovered = HitTestConnectionControl(*connection, geometry, input.MouseX, input.MouseY);
    int32 newHover = hovered ? hovered->Id : 0;
    bool changed = newHover != InteractionState.HoverControlId;
    InteractionState.HoverControlId = newHover;
    if (input.LeftPressed) {
        InteractionState.PressedControlId = newHover;
        changed = true;
    }
    if (input.LeftReleased) {
        int32 released = newHover;
        int32 pressed = InteractionState.PressedControlId;
        InteractionState.PressedControlId = 0;
        changed = true;
        if (released != 0 && released == pressed) {
            if (IsEditControlId(released)) {
                InteractionState.FocusedControlId = released;
                InteractionState.LastAction = released == 7 ? "focus_login" : "focus_password";
            } else if (IsCheckControlId(released)) {
                InteractionState.SaveLogin = !InteractionState.SaveLogin;
                InteractionState.LastAction = InteractionState.SaveLogin ? "save_login_on" : "save_login_off";
            } else {
                InteractionState.LastAction = "click_control_" + std::to_string(released);
                if (logger) { logger->Info("UI click: connection control id=" + std::to_string(released)); }
            }
        }
    }
    if (IsEditControlId(InteractionState.FocusedControlId)) {
        std::string& target = InteractionState.FocusedControlId == 7 ? InteractionState.LoginText : InteractionState.PasswordText;
        if (input.BackspacePressed && !target.empty()) { target.pop_back(); changed = true; }
        if (!input.TypedText.empty()) { target += input.TypedText; if (target.size() > 256) { target.resize(256); } changed = true; }
        if (input.TabPressed) { InteractionState.FocusedControlId = InteractionState.FocusedControlId == 7 ? 8 : 7; changed = true; }
    }
    if (input.EnterPressed) { InteractionState.LastAction = "login_requested"; changed = true; if (logger) { logger->Info("UI action: login_requested"); } }
    return changed;
}

void FLoadingScreenModel::SetStage(std::string stage, float progress) { CurrentStage = std::move(stage); CurrentProgress = std::clamp(progress, 0.0f, 1.0f); }
void FLoadingScreenModel::AddStatusLine(std::string line) { StatusLines.push_back(std::move(line)); if (StatusLines.size() > 8) { StatusLines.erase(StatusLines.begin()); } }
FLoadingScreenPainter::FLoadingScreenPainter(const FLoadingScreenModel& model) : Model(model) {}

void FLoadingScreenPainter::Paint(HDC__* dc, const tagRECT& rect) const {
    RECT r = rect;
    HBRUSH background = CreateSolidBrush(RGB(7, 10, 16));
    FillRect(dc, &r, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 224, 232));
    HFONT titleFont = CreateFontA(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT bodyFont = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    RECT title{40, 36, rect.right - 40, 82};
    DrawTextA(dc, "Sfera Client x64", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, bodyFont);
    std::string stage = "stage: " + Model.Stage();
    RECT stageRect{40, 94, rect.right - 40, 124};
    DrawTextA(dc, stage.c_str(), -1, &stageRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    bool usedLayoutBar = false;
    int y = 188;
    if (const FUiWindow* window = Model.LayoutWindow()) {
        int designWidth = window->Size.X > 0 ? window->Size.X : 1024;
        int designHeight = window->Size.Y > 0 ? window->Size.Y : 768;
        if (const FUiControl* progress = FindFirstControl(*window, "PROGRESS_BAR")) {
            RECT border = ScaleRect(progress->Position.X, progress->Position.Y, progress->Size.X, progress->Size.Y, designWidth, designHeight, rect);
            HBRUSH borderBrush = CreateSolidBrush(RGB(120, 128, 148));
            FrameRect(dc, &border, borderBrush);
            DeleteObject(borderBrush);
            int fillRight = border.left + static_cast<int>((border.right - border.left - 2) * Model.Progress());
            RECT fill{border.left + 1, border.top + 1, fillRight, border.bottom - 1};
            HBRUSH fillBrush = CreateSolidBrush(RGB(190, 170, 48));
            FillRect(dc, &fill, fillBrush);
            DeleteObject(fillBrush);
            usedLayoutBar = true;
        }
        if (const FUiControl* textList = FindFirstControl(*window, "TEXTLIST")) { y = ScaleY(textList->Position.Y, designHeight, rect); }
    }
    if (!usedLayoutBar) {
        int barLeft = 40;
        int barTop = 140;
        int barRight = rect.right - 40;
        int barBottom = 162;
        RECT border{barLeft, barTop, barRight, barBottom};
        HBRUSH borderBrush = CreateSolidBrush(RGB(90, 96, 112));
        FrameRect(dc, &border, borderBrush);
        DeleteObject(borderBrush);
        int fillRight = barLeft + static_cast<int>((barRight - barLeft - 2) * Model.Progress());
        RECT fill{barLeft + 1, barTop + 1, fillRight, barBottom - 1};
        HBRUSH fillBrush = CreateSolidBrush(RGB(80, 140, 220));
        FillRect(dc, &fill, fillBrush);
        DeleteObject(fillBrush);
    }
    std::string ui = std::string("loadscreen ui: ") + (Model.HasParsedLayout() ? "parsed layout" : (Model.HasUiFile() ? "resource-backed" : "fallback"));
    RECT lineRect{40, y, rect.right - 40, y + 24};
    DrawTextA(dc, ui.c_str(), -1, &lineRect, DT_LEFT | DT_SINGLELINE);
    y += 30;
    if (Model.HasParsedLayout()) {
        const auto& stats = Model.UiStats();
        std::string statsLine = "ui stats: windows=" + std::to_string(stats.WindowCount) + " controls=" + std::to_string(stats.ControlCount) + " sprites=" + std::to_string(stats.SpriteCount) + " textures=" + std::to_string(stats.TextureNameCount);
        RECT lr{40, y, rect.right - 40, y + 22};
        DrawTextA(dc, statsLine.c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 22;
    }
    for (const auto& ref : Model.Resources()) {
        std::string line = ref.LogicalName + " [" + ref.Kind + "] bytes=" + std::to_string(ref.Size);
        RECT lr{40, y, rect.right - 40, y + 22};
        DrawTextA(dc, line.c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 22;
    }
    y += 18;
    for (const auto& line : Model.Lines()) {
        RECT lr{40, y, rect.right - 40, y + 22};
        DrawTextA(dc, line.c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 22;
    }
    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
}
void FLoadingScreenPainter::PaintConnectionTextOverlay(HDC__* dc, const tagRECT& rect) const {
    const FUiWindow* load = Model.LayoutWindow();
    const FUiWindow* connection = Model.ConnectionWindow();
    if (!load || !connection) { return; }
    int designWidth = load->Size.X > 0 ? load->Size.X : 1024;
    int designHeight = load->Size.Y > 0 ? load->Size.Y : 768;
    double scale = std::min((rect.right - rect.left) / static_cast<double>(designWidth), (rect.bottom - rect.top) / static_cast<double>(designHeight));
    int ox = static_cast<int>((rect.right - rect.left - designWidth * scale) * 0.5);
    int oy = static_cast<int>((rect.bottom - rect.top - designHeight * scale) * 0.5);
    int wx = ox + static_cast<int>((designWidth - connection->Size.X) * 0.5 * scale);
    int wy = oy + static_cast<int>((designHeight - connection->Size.Y) * 0.5 * scale - 10.0 * scale);
    auto sx = [&](int v) { return static_cast<int>(v * scale); };
    auto draw = [&](int x, int y, int w, int h, const wchar_t* text, bool center, COLORREF color) {
        RECT tr{wx + sx(x), wy + sx(y), wx + sx(x + w), wy + sx(y + h)};
        SetTextColor(dc, color);
        DrawTextW(dc, text, -1, &tr, (center ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    };
    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(std::max(11, sx(13)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Georgia");
    HFONT bodyFont = CreateFontW(std::max(10, sx(11)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    draw(12, 8, 170, 20, L"Подключение", false, RGB(237, 208, 161));
    SelectObject(dc, bodyFont);
    draw(39, 34, 141, 14, L"Имя пользователя:", true, RGB(255, 255, 255));
    draw(39, 79, 141, 15, L"Пароль:", true, RGB(255, 255, 255));
    draw(70, 122, 114, 15, L"Сохранить логин", false, RGB(255, 255, 255));
    draw(32, 150, 61, 20, L"OK", true, RGB(237, 208, 161));
    draw(109, 150, 65, 20, L"Отмена", true, RGB(237, 208, 161));
    auto toWide = [](const std::string& text) { return std::wstring(text.begin(), text.end()); };
    const FUiInteractionState& state = Model.Interaction();
    RECT loginText{wx + sx(51), wy + sx(53), wx + sx(164), wy + sx(67)};
    SetTextColor(dc, RGB(237, 208, 161));
    std::wstring login = toWide(state.LoginText);
    DrawTextW(dc, login.c_str(), -1, &loginText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT passwordText{wx + sx(49), wy + sx(99), wx + sx(162), wy + sx(113)};
    std::wstring password(state.PasswordText.size(), L'•');
    DrawTextW(dc, password.c_str(), -1, &passwordText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
}

}
