#pragma once
#include "UI/UiRuntime.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"
#include <algorithm>
#include <cwctype>
#include <utility>

struct FUiRuntimeInternals
{
static bool IsCharacterNameChar(wchar_t ch)
{
    if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) { return true; }
    if (ch >= L'А' && ch <= L'я') { return true; }
    if (ch == L'Ё' || ch == L'ё') { return true; }
    return std::iswalnum(ch) != 0;
}

static std::wstring LowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

static bool Utf8EqualsWideNoCase(std::string_view utf8, std::wstring_view wide)
{
    return LowerWide(Common::Utf8ToWide(utf8)) == LowerWide(std::wstring(wide));
}

static void RemoveLastUtf8Codepoint(std::string& text)
{
    if (text.empty()) { return; }
    size_t pos = text.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xc0U) == 0x80U) { --pos; }
    text.resize(pos);
}

static void AppendUtf8Limited(std::string& target, std::string_view typed, size_t maxChars)
{
    if (typed.empty()) { return; }
    std::wstring wide = Common::Utf8ToWide(target);
    const std::wstring add = Common::Utf8ToWide(typed);
    for (wchar_t ch : add) { if (wide.size() < maxChars) { wide.push_back(ch); } }
    target = Common::WideToUtf8(wide);
}

static std::string TrimUtf8ToCodepoints(std::string text, size_t maxChars)
{
    std::wstring wide = Common::Utf8ToWide(text);
    if (wide.size() > maxChars) { wide.resize(maxChars); }
    return Common::WideToUtf8(wide);
}

static int32 CycleIndex(int32 value, int32 count, int32 delta)
{
    if (count <= 0) { return 0; }
    value = (value + (delta < 0 ? -1 : 1)) % count;
    return value < 0 ? value + count : value;
}

static bool ApplyUtf8TextEdit(std::string& target, const FInputSnapshot& input, size_t maxChars)
{
    bool changed = false;
    if (input.BackspacePressed && !target.empty()) { RemoveLastUtf8Codepoint(target); changed = true; }
    if (!input.TypedText.empty()) { AppendUtf8Limited(target, input.TypedText, maxChars); changed = true; }
    return changed;
}

template <class AcceptChar>
static bool ApplyWideTextEdit(std::wstring& target, const FInputSnapshot& input, size_t maxChars, AcceptChar acceptChar)
{
    bool changed = false;
    if (input.BackspacePressed && !target.empty()) { target.pop_back(); changed = true; }
    const std::wstring typed = Common::Utf8ToWide(input.TypedText);
    for (wchar_t ch : typed) { if (target.size() < maxChars && acceptChar(ch)) { target.push_back(ch); changed = true; } }
    return changed;
}

static bool IsMouseControlClass(std::string_view classId)
{
    return Common::EqualsNoCase(classId, "BUTTON") || Common::EqualsNoCase(classId, "CHECKBOX") || Common::EqualsNoCase(classId, "EDIT") || Common::EqualsNoCase(classId, "RADIOBUTTON") || Common::EqualsNoCase(classId, "SPINBUTTON");
}

static bool ControlCanReceiveMouse(const FUiControlDef& control)
{
    return !control.Hidden && !control.Disabled && IsMouseControlClass(control.ClassId);
}

static bool IsSpinButton(const FUiControlDef& control)
{
    return Common::EqualsNoCase(control.ClassId, "SPINBUTTON");
}

static bool Contains(const FUiRectF& rect, int32 x, int32 y)
{
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    return fx >= rect.X && fy >= rect.Y && fx < rect.X + rect.W && fy < rect.Y + rect.H;
}

static FUiRectF ControlRectInWindow(const FUiRectF& windowRect, const FUiControlDef& control, float scale = 1.0f)
{
    return FUiRectF{windowRect.X + static_cast<float>(control.Rect.X) * scale, windowRect.Y + static_cast<float>(control.Rect.Y) * scale, static_cast<float>(std::max(1, control.Rect.W)) * scale, static_cast<float>(std::max(1, control.Rect.H)) * scale};
}
};
