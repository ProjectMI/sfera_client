#include "UI/UiRuntime.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace Sfera {
namespace {
constexpr int32 LoginEditId = 7;
constexpr int32 PasswordEditId = 8;
constexpr int32 SavePasswordId = 9;
constexpr int32 LoginButtonId = 3;
constexpr int32 CancelButtonId = 1;
constexpr int32 QuitButtonId = 4;
constexpr int32 RegistrationButtonId = 11;

std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }); return value; }

std::string Trim(std::string value) {
    auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool EqualsNoCase(std::string_view a, std::string_view b) { return Lower(std::string(a)) == Lower(std::string(b)); }

std::string StripComment(std::string line) {
    bool inQuote = false;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == '"') { inQuote = !inQuote; }
        if (!inQuote && line[i] == '/' && line[i + 1] == '/') { line.resize(i); break; }
    }
    return line;
}

std::string ParseQuoted(const std::string& line) {
    const size_t begin = line.find('"');
    if (begin == std::string::npos) { return {}; }
    const size_t end = line.find('"', begin + 1);
    if (end == std::string::npos) { return {}; }
    return line.substr(begin + 1, end - begin - 1);
}

bool StartsWithToken(const std::string& line, std::string_view token) { std::istringstream input(line); std::string first; input >> first; return EqualsNoCase(first, token); }

bool IsOpenBrace(const std::string& line) { return line == "{"; }
bool IsCloseBrace(const std::string& line) { return line == "}"; }

bool ParseBool(std::istringstream& input, bool defaultValue = false) {
    std::string value;
    input >> value;
    value = Lower(std::move(value));
    if (value == "true" || value == "1") { return true; }
    if (value == "false" || value == "0") { return false; }
    return defaultValue;
}

FUiColor ParseColor(std::istringstream& input, FUiColor defaultValue) { FUiColor color = defaultValue; input >> color.R >> color.G >> color.B >> color.A; return color; }

std::vector<std::string> LinesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') { line.pop_back(); } lines.push_back(std::move(line)); }
    return lines;
}

int32 ParseControlIdFromComment(const std::string& line) {
    const size_t marker = Lower(line).find("id");
    if (marker == std::string::npos) { return 0; }
    const size_t eq = line.find('=', marker);
    if (eq == std::string::npos) { return 0; }
    std::istringstream input(line.substr(eq + 1));
    int32 id = 0;
    input >> id;
    return id;
}

TResult<std::string> DecodeCp1251ToUtf8(const FByteArray& bytes) {
    if (bytes.empty()) { return std::string{}; }
    const int wideCount = MultiByteToWideChar(1251, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideCount <= 0) { return FStatus::Error(EStatusCode::InvalidData, "cp1251 decode failed"); }
    std::wstring wide(static_cast<size_t>(wideCount), L'\0');
    MultiByteToWideChar(1251, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), wide.data(), wideCount);
    const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Count <= 0) { return FStatus::Error(EStatusCode::InvalidData, "utf8 encode failed"); }
    std::string utf8(static_cast<size_t>(utf8Count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), utf8Count, nullptr, nullptr);
    return utf8;
}

TResult<std::string> LoadUiText(const FResourceManager& resources, std::string_view logicalName) {
    auto blob = resources.Load(logicalName);
    if (!blob.IsOk()) { return blob.Status(); }
    return DecodeCp1251ToUtf8(blob.Value().Bytes);
}

bool ControlCanReceiveMouse(const FUiControlDef& control) {
    if (control.Hidden || control.Disabled) { return false; }
    return EqualsNoCase(control.ClassId, "BUTTON") || EqualsNoCase(control.ClassId, "CHECKBOX") || EqualsNoCase(control.ClassId, "EDIT");
}
}

TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName) {
    auto text = LoadUiText(resources, logicalName);
    if (!text.IsOk()) { return text.Status(); }
    FUiStringTable strings;
    for (auto line : LinesOf(text.Value())) {
        line = Trim(StripComment(std::move(line)));
        if (!StartsWithToken(line, "string")) { continue; }
        std::istringstream input(line);
        std::string keyword;
        std::string key;
        input >> keyword >> key;
        std::string value = ParseQuoted(line);
        if (!key.empty()) { strings[std::move(key)] = std::move(value); }
    }
    return strings;
}

TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName) {
    auto text = LoadUiText(resources, logicalName);
    if (!text.IsOk()) { return text.Status(); }
    FUiWindowDef window;
    bool pendingSprite = false;
    bool inSprite = false;
    FUiSpriteDef sprite;
    bool pendingControl = false;
    int32 pendingId = 0;
    bool inControl = false;
    int32 controlNestedDepth = 0;
    std::string pendingControlSection;
    std::string activeControlSection;
    FUiControlDef control;
    for (const auto& rawLine : LinesOf(text.Value())) {
        std::string line = Trim(StripComment(rawLine));
        if (line.empty()) { continue; }
        if (!inControl && !inSprite && StartsWithToken(line, "sprite")) { pendingSprite = true; continue; }
        if (!inControl && !inSprite && Lower(rawLine).find("control") != std::string::npos) { pendingControl = true; pendingId = ParseControlIdFromComment(rawLine); }
        if (pendingSprite && IsOpenBrace(line)) { pendingSprite = false; inSprite = true; sprite = FUiSpriteDef{}; continue; }
        if (inSprite) {
            if (IsCloseBrace(line)) { if (!sprite.Name.empty()) { window.Sprites[Lower(sprite.Name)] = std::move(sprite); } inSprite = false; continue; }
            std::istringstream input(line);
            std::string key;
            input >> key;
            key = Lower(std::move(key));
            if (key == "name") { sprite.Name = ParseQuoted(line); }
            else if (key == "size") { input >> sprite.Width >> sprite.Height; }
            else if (key == "texture") { FUiSpritePiece piece; piece.TextureName = ParseQuoted(line); const size_t endQuote = line.find('"', line.find('"') + 1); if (endQuote != std::string::npos) { std::istringstream coords(line.substr(endQuote + 1)); coords >> piece.SrcLeft >> piece.SrcTop >> piece.SrcRight >> piece.SrcBottom >> piece.DstLeft >> piece.DstTop >> piece.DstRight >> piece.DstBottom; sprite.Pieces.push_back(std::move(piece)); } }
            else if (key == "tcoords") { int32 index = -1; input >> index; if (index >= 0 && index < static_cast<int32>(sprite.Pieces.size())) { auto& piece = sprite.Pieces[static_cast<size_t>(index)]; for (auto& coord : piece.TexCoords) { input >> coord.U >> coord.V; } piece.HasTexCoords = true; } }
            continue;
        }
        if (pendingControl && IsOpenBrace(line)) { inControl = true; pendingControl = false; controlNestedDepth = 0; control = FUiControlDef{}; control.Id = pendingId; continue; }
        if (inControl) {
            const std::string lowerLine = Lower(line);
            if (controlNestedDepth == 0 && (lowerLine == "leftbutton" || lowerLine == "rightbutton")) { pendingControlSection = lowerLine; continue; }
            if (IsOpenBrace(line)) { ++controlNestedDepth; if (controlNestedDepth == 1 && !pendingControlSection.empty()) { activeControlSection = std::move(pendingControlSection); pendingControlSection.clear(); } continue; }
            if (IsCloseBrace(line)) { if (controlNestedDepth > 0) { --controlNestedDepth; if (controlNestedDepth == 0) { activeControlSection.clear(); } continue; } if (control.Id == 0) { control.Id = -1 - static_cast<int32>(window.Controls.size()); } window.Controls.push_back(std::move(control)); inControl = false; continue; }
            if (controlNestedDepth > 0) {
                if (controlNestedDepth == 1 && !activeControlSection.empty()) {
                    FUiSubButtonDef& button = activeControlSection == "leftbutton" ? control.LeftButton : control.RightButton;
                    std::istringstream input(line);
                    std::string key;
                    input >> key;
                    key = Lower(std::move(key));
                    if (key == "position") { input >> button.X >> button.Y; }
                    else if (key == "size") { input >> button.W >> button.H; }
                    else if (key == "checkedimage") { button.CheckedImage = ParseQuoted(line); }
                    else if (key == "focusedimage") { button.FocusedImage = ParseQuoted(line); }
                    else if (key == "disabledimage") { button.DisabledImage = ParseQuoted(line); }
                }
                continue;
            }
            std::istringstream input(line);
            std::string key;
            input >> key;
            key = Lower(std::move(key));
            if (key == "classid") { input >> control.ClassId; }
            else if (key == "position") { input >> control.Rect.X >> control.Rect.Y; }
            else if (key == "size") { input >> control.Rect.W >> control.Rect.H; }
            else if (key == "font") { input >> control.Font; }
            else if (key == "textcolor") { control.TextColor = ParseColor(input, control.TextColor); }
            else if (key == "disabledcolor") { control.DisabledColor = ParseColor(input, control.DisabledColor); }
            else if (key == "focuscolor") { control.FocusColor = ParseColor(input, control.FocusColor); }
            else if (key == "windowtext") { input >> control.TextKey; if (control.TextKey.empty()) { control.TextKey = ParseQuoted(line); } }
            else if (key == "textformat") { control.TextCenter = line.find("CENTER") != std::string::npos; }
            else if (key == "checkedimage") { control.CheckedImage = ParseQuoted(line); }
            else if (key == "uncheckedimage") { control.UncheckedImage = ParseQuoted(line); }
            else if (key == "focusedimage") { control.FocusedImage = ParseQuoted(line); }
            else if (key == "image") { control.ImageName = ParseQuoted(line); }
            else if (key == "drawmethod") { control.DrawSpriteName = ParseQuoted(line); }
            else if (key == "windowhelp") { control.WindowHelp = ParseQuoted(line); }
            else if (key == "slotempty") { control.SlotEmptyImage = ParseQuoted(line); }
            else if (key == "slotfull") { control.SlotFullImage = ParseQuoted(line); }
            else if (key == "slotborder") { control.SlotBorderImage = ParseQuoted(line); }
            else if (key == "scrollspr") { control.ScrollSpriteName = ParseQuoted(line); const size_t endQuote = line.find('"', line.find('"') + 1); if (endQuote != std::string::npos) { std::istringstream sizeInput(line.substr(endQuote + 1)); sizeInput >> control.ScrollSpriteWidth >> control.ScrollSpriteHeight; } }
            else if (key == "deltastep") { input >> control.DeltaStep; }
            else if (key == "password") { control.Password = ParseBool(input, control.Password); }
            else if (key == "buttonstyle") { control.SendQuit = line.find("SEND_QUIT") != std::string::npos; control.SendHelp = line.find("SEND_HELP") != std::string::npos; }
            else if (key == "hidden") { control.Hidden = ParseBool(input, control.Hidden); }
            else if (key == "disabled") { control.Disabled = ParseBool(input, control.Disabled); }
            continue;
        }
        std::istringstream input(line);
        std::string key;
        input >> key;
        key = Lower(std::move(key));
        if (key == "windowname") { window.Name = ParseQuoted(line); }
        else if (!window.Name.empty() && key == "windowtext") { input >> window.TextKey; if (window.TextKey.empty()) { window.TextKey = ParseQuoted(line); } }
        else if (!window.Name.empty() && key == "position") { input >> window.Rect.X >> window.Rect.Y; }
        else if (!window.Name.empty() && key == "size") { input >> window.Rect.W >> window.Rect.H; }
        else if (!window.Name.empty() && key == "font") { input >> window.Font; }
        else if (!window.Name.empty() && key == "textcolor") { window.TextColor = ParseColor(input, window.TextColor); }
        else if (!window.Name.empty() && key == "recttitle") { input >> window.TitleRect.X >> window.TitleRect.Y >> window.TitleRect.W >> window.TitleRect.H; }
        else if (!window.Name.empty() && key == "alignwin") { window.AlignCenterX = line.find("CENTER_X") != std::string::npos; window.AlignCenterY = line.find("CENTER_Y") != std::string::npos; window.AlignRightX = line.find("RIGHT_X") != std::string::npos; window.AlignRightY = line.find("RIGHT_Y") != std::string::npos; }
        else if (!window.Name.empty() && key == "savelastposition") { window.SaveLastPosition = ParseBool(input, window.SaveLastPosition); }
        else if (!window.Name.empty() && key == "candragdrop") { window.CanDragDrop = ParseBool(input, window.CanDragDrop); }
        else if (!window.Name.empty() && key == "cannotcross") { window.CanNotCross = ParseBool(input, window.CanNotCross); }
        else if (!window.Name.empty() && key == "cangotop") { window.CanGoTop = ParseBool(input, window.CanGoTop); }
        else if (!window.Name.empty() && key == "escapehandle") { window.EscapeHandle = ParseBool(input, window.EscapeHandle); }
        else if (!window.Name.empty() && key == "drawmethod") { window.DrawNone = line.find("NONE") != std::string::npos; window.DrawSpriteName = ParseQuoted(line); }
    }
    if (window.Name.empty()) { return FStatus::Error(EStatusCode::InvalidData, "UI window has no windowName: " + std::string(logicalName)); }
    return window;
}

FStatus FUiRuntime::Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger) {
    Bootstrap = desc;
    auto strings = LoadUiStringTableFromResource(resources, Bootstrap.StringsResource);
    if (!strings.IsOk()) { return strings.Status(); }
    auto connection = LoadUiWindowFromResource(resources, Bootstrap.ConnectionWindowResource);
    if (!connection.IsOk()) { return connection.Status(); }
    if (!resources.Catalog().FindByLogicalName(Bootstrap.LoginBackgroundTexture)) { return FStatus::Error(EStatusCode::NotFound, "login background texture is not cataloged: " + Bootstrap.LoginBackgroundTexture); }
    StringTable = std::move(strings.Value());
    Connection = std::move(connection.Value());
    Ready = true;
    AddStatusLine("ui: connection window loaded, controls=" + std::to_string(Connection.Controls.size()) + ", sprites=" + std::to_string(Connection.Sprites.size()));
    if (logger) { logger->Info("UI runtime initialized: strings=" + std::to_string(StringTable.size()) + ", connection=" + Connection.Name + ", controls=" + std::to_string(Connection.Controls.size())); }
    return FStatus::Ok();
}

void FUiRuntime::SetStage(std::string stage, float progress) { CurrentStage = std::move(stage); CurrentProgress = std::clamp(progress, 0.0f, 1.0f); }

void FUiRuntime::AddStatusLine(std::string line) { Status.push_back(std::move(line)); if (Status.size() > 6) { Status.erase(Status.begin()); } }

std::string FUiRuntime::ResolveText(std::string_view key) const {
    auto it = StringTable.find(std::string(key));
    if (it == StringTable.end()) { return {}; }
    return it->second;
}

FUiRectF FUiRuntime::BuildDesignRect(const tagRECT& clientRect) const {
    const float clientW = static_cast<float>(clientRect.right - clientRect.left);
    const float clientH = static_cast<float>(clientRect.bottom - clientRect.top);
    const float designW = static_cast<float>(Bootstrap.DesignWidth > 0 ? Bootstrap.DesignWidth : 1024);
    const float designH = static_cast<float>(Bootstrap.DesignHeight > 0 ? Bootstrap.DesignHeight : 768);
    return FUiRectF{std::floor((clientW - designW) * 0.5f), std::floor((clientH - designH) * 0.5f), designW, designH};
}

FUiRectF FUiRuntime::BuildConnectionRect(const tagRECT& clientRect) const {
    FUiRectF design = BuildDesignRect(clientRect);
    const float w = static_cast<float>(Connection.Rect.W);
    const float h = static_cast<float>(Connection.Rect.H);
    return FUiRectF{std::floor(design.X + (design.W - w) * 0.5f), std::floor(design.Y + (design.H - h) * 0.5f), w, h};
}

const FUiControlDef* FUiRuntime::HitTestConnection(int32 x, int32 y, const tagRECT& clientRect) const {
    if (!Ready) { return nullptr; }
    FUiRectF wr = BuildConnectionRect(clientRect);
    const float scale = wr.W / static_cast<float>(std::max(1, Connection.Rect.W));
    for (auto it = Connection.Controls.rbegin(); it != Connection.Controls.rend(); ++it) {
        const FUiControlDef& control = *it;
        if (!ControlCanReceiveMouse(control)) { continue; }
        FUiRectF r{wr.X + control.Rect.X * scale, wr.Y + control.Rect.Y * scale, control.Rect.W * scale, control.Rect.H * scale};
        if (static_cast<float>(x) >= r.X && static_cast<float>(y) >= r.Y && static_cast<float>(x) < r.X + r.W && static_cast<float>(y) < r.Y + r.H) { return &control; }
    }
    return nullptr;
}

bool FUiRuntime::IsEditControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "EDIT"); }
bool FUiRuntime::IsCheckControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "CHECKBOX") || control.Id == SavePasswordId; }
bool FUiRuntime::IsButtonControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "BUTTON"); }

void FUiRuntime::ActivateControl(const FUiControlDef& control, FLogger* logger) {
    Actions.LastControlId = control.Id;
    if (IsEditControl(control)) { Actions.FocusedControlId = control.Id; Actions.LastAction = control.Id == PasswordEditId || control.Password ? "focus_password" : "focus_login"; return; }
    if (IsCheckControl(control)) { Actions.SaveLogin = !Actions.SaveLogin; Actions.LastAction = Actions.SaveLogin ? "save_login_on" : "save_login_off"; return; }
    if (IsButtonControl(control)) {
        if (control.Id == LoginButtonId) { Actions.LastAction = "login_requested"; }
        else if (control.Id == CancelButtonId || control.Id == QuitButtonId || control.SendQuit) { Actions.LastAction = "quit_requested"; }
        else if (control.Id == RegistrationButtonId) { Actions.LastAction = "registration_requested"; }
        else { Actions.LastAction = "click_control_" + std::to_string(control.Id); }
        if (logger) { logger->Info("UI action: " + Actions.LastAction); }
    }
}

bool FUiRuntime::HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger) {
    if (!Ready) { return false; }
    bool changed = false;
    const FUiControlDef* hovered = HitTestConnection(input.MouseX, input.MouseY, clientRect);
    const int32 newHover = hovered ? hovered->Id : 0;
    if (newHover != Actions.HoverControlId) { Actions.HoverControlId = newHover; changed = true; }
    if (input.LeftPressed) { Actions.PressedControlId = newHover; changed = true; }
    if (input.LeftReleased) {
        const int32 pressed = Actions.PressedControlId;
        Actions.PressedControlId = 0;
        changed = true;
        if (hovered && hovered->Id != 0 && hovered->Id == pressed) { ActivateControl(*hovered, logger); }
    }
    if (Actions.FocusedControlId == LoginEditId || Actions.FocusedControlId == PasswordEditId) {
        std::string& target = Actions.FocusedControlId == PasswordEditId ? Actions.PasswordText : Actions.LoginText;
        if (input.BackspacePressed && !target.empty()) { target.pop_back(); changed = true; }
        if (!input.TypedText.empty()) { target += input.TypedText; if (target.size() > 256) { target.resize(256); } changed = true; }
        if (input.TabPressed) { Actions.FocusedControlId = Actions.FocusedControlId == LoginEditId ? PasswordEditId : LoginEditId; changed = true; }
    }
    if (input.EnterPressed) { Actions.LastAction = "login_requested"; changed = true; if (logger) { logger->Info("UI action: login_requested"); } }
    return changed;
}
}
