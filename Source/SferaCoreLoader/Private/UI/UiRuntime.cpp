#include "UI/UiRuntime.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr int32 LoginEditId = 7;
    constexpr int32 PasswordEditId = 8;
    constexpr int32 SavePasswordId = 9;
    constexpr int32 LoginButtonId = 3;
    constexpr int32 CancelButtonId = 1;
    constexpr int32 QuitButtonId = 4;
    constexpr int32 RegistrationButtonId = 11;
    constexpr int32 CharacterDeleteButtonId = 57;
    constexpr int32 CharacterExitButtonId = 58;
    constexpr int32 CharacterContinueButtonId = 59;
    constexpr size_t MaxLoginChars = 63;
    constexpr size_t MaxPasswordChars = 29;
    constexpr size_t MaxCharacterNameChars = 15;

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::string Trim(std::string value)
    {
        auto notSpace = [](unsigned char ch)
        {
            return std::isspace(ch) == 0;
        };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    bool EqualsNoCase(std::string_view a, std::string_view b) { return Lower(std::string(a)) == Lower(std::string(b)); }

    int32 ClampIndexToCount(int32 value, int32 count) { return count <= 0 ? 0 : std::clamp(value, 0, count - 1); }

    std::string StripComment(std::string line)
    {
        bool inQuote = false;

        for (size_t i = 0; i + 1 < line.size(); ++i)
        {
            if (line[i] == '"')
            {
                inQuote = !inQuote;
            }

            if (!inQuote && line[i] == '/' && line[i + 1] == '/') { line.resize(i); break; }
        }

        return line;
    }

    std::string ParseQuoted(const std::string& line)
    {
        const size_t begin = line.find('"');

        if (begin == std::string::npos) { return {}; }

        const size_t end = line.find('"', begin + 1);

        if (end == std::string::npos) { return {}; }

        return line.substr(begin + 1, end - begin - 1);
    }

    bool StartsWithToken(const std::string& line, std::string_view token)
    {
        std::istringstream input(line);
        std::string first;
        input >> first;
        return EqualsNoCase(first, token);
    }

    bool IsOpenBrace(const std::string& line) { return line == "{"; }
    bool IsCloseBrace(const std::string& line) { return line == "}"; }

    bool ParseBool(std::istringstream& input, bool defaultValue = false)
    {
        std::string value;
        input >> value;
        value = Lower(std::move(value));

        if (value == "true" || value == "1") { return true; }

        if (value == "false" || value == "0") { return false; }

        return defaultValue;
    }

    FUiColor ParseColor(std::istringstream& input, FUiColor defaultValue)
    {
        FUiColor color = defaultValue;
        input >> color.R >> color.G >> color.B >> color.A;
        return color;
    }

    std::vector<std::string> LinesOf(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream input(text);
        std::string line;

        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            lines.push_back(std::move(line));
        }

        return lines;
    }

    int32 ParseControlIdFromComment(const std::string& line)
    {
        const size_t marker = Lower(line).find("id");

        if (marker == std::string::npos) { return 0; }

        const size_t eq = line.find('=', marker);

        if (eq == std::string::npos) { return 0; }

        std::istringstream input(line.substr(eq + 1));
        int32 id = 0;
        input >> id;
        return id;
    }

    TResult<std::string> DecodeCp1251ToUtf8(const FByteArray& bytes)
    {
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

    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty()) { return {}; }

        const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(std::max(0, required)), '\0');

        if (required > 0)
        {
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), required, nullptr, nullptr);
        }

        return utf8;
    }

    std::wstring Utf8ToWideLocal(const std::string& text)
    {
        if (text.empty()) { return {}; }

        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

        if (required <= 0) { return {}; }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), required);
        return wide;
    }

    std::string TrimUtf8ToCodepoints(std::string text, size_t maxChars)
    {
        if (text.empty()) { return {}; }

        std::wstring wide = Utf8ToWideLocal(text);

        if (wide.size() > maxChars)
        {
            wide.resize(maxChars);
        }

        return WideToUtf8(wide);
    }

    void AppendUtf8Limited(std::string& target, std::string_view typed, size_t maxChars)
    {
        if (typed.empty()) { return; }

        std::wstring wide = Utf8ToWideLocal(target);
        std::wstring add = Utf8ToWideLocal(std::string(typed));

        for (wchar_t ch : add)
        {
            if (wide.size() < maxChars)
            {
                wide.push_back(ch);
            }
        }

        target = WideToUtf8(wide);
    }

    bool Utf8EqualsWideNoCase(const std::string& utf8, const std::wstring& wide)
    {
        std::wstring lhs = Utf8ToWideLocal(utf8);
        std::wstring rhs = wide;
        auto lowerWide = [](std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            return value;
        };
        return lowerWide(std::move(lhs)) == lowerWide(std::move(rhs));
    }

    void RemoveLastUtf8Codepoint(std::string& text)
    {
        if (text.empty()) { return; }

        size_t pos = text.size() - 1;

        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xc0U) == 0x80U)
        {
            --pos;
        }

        text.resize(pos);
    }

    bool IsCharacterNameChar(wchar_t ch)
    {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) { return true; }

        if (ch >= L'А' && ch <= L'я') { return true; }

        if (ch == L'Ё' || ch == L'ё') { return true; }

        return std::iswalnum(ch) != 0;
    }

    TResult<std::string> LoadUiText(const FResourceManager& resources, std::string_view logicalName)
    {
        auto blob = resources.Load(logicalName);

        if (!blob.IsOk()) { return blob.Status(); }

        return DecodeCp1251ToUtf8(blob.Value().Bytes);
    }

    bool ControlCanReceiveMouse(const FUiControlDef& control)
    {
        if (control.Hidden || control.Disabled) { return false; }

        return EqualsNoCase(control.ClassId, "BUTTON") || EqualsNoCase(control.ClassId, "CHECKBOX") || EqualsNoCase(control.ClassId, "EDIT") || EqualsNoCase(control.ClassId, "RADIOBUTTON") || EqualsNoCase(control.ClassId, "SPINBUTTON");
    }
}

TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    auto text = LoadUiText(resources, logicalName);

    if (!text.IsOk()) { return text.Status(); }

    FUiStringTable strings;

    for (auto line : LinesOf(text.Value()))
    {
        line = Trim(StripComment(std::move(line)));

        if (!StartsWithToken(line, "string")) { continue; }

        std::istringstream input(line);
        std::string keyword;
        std::string key;
        input >> keyword >> key;
        std::string value = ParseQuoted(line);

        if (!key.empty())
        {
            strings[std::move(key)] = std::move(value);
        }
    }

    return strings;
}

TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName)
{
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

    for (const auto& rawLine : LinesOf(text.Value()))
    {
        std::string line = Trim(StripComment(rawLine));

        if (line.empty()) { continue; }

        if (!inControl && !inSprite && !window.Name.empty() && IsCloseBrace(line)) { break; }

        if (!inControl && !inSprite && StartsWithToken(line, "sprite")) { pendingSprite = true; continue; }

        if (!inControl && !inSprite && Lower(rawLine).find("control") != std::string::npos)
        {
            pendingControl = true;
            pendingId = ParseControlIdFromComment(rawLine);
        }

        if (pendingSprite && IsOpenBrace(line))
        {
            pendingSprite = false;
            inSprite = true;
            sprite = FUiSpriteDef{};
            continue;
        }

        if (inSprite)
        {
            if (IsCloseBrace(line))
            {
                if (!sprite.Name.empty())
                {
                    window.Sprites[Lower(sprite.Name)] = std::move(sprite);
                }

                inSprite = false;
                continue;
            }

            std::istringstream input(line);
            std::string key;
            input >> key;
            key = Lower(std::move(key));

            if (key == "name")
            {
                sprite.Name = ParseQuoted(line);
            }
            else if (key == "size")
            {
                input >> sprite.Width >> sprite.Height;
            }
            else if (key == "texture")
            {
                FUiSpritePiece piece;
                piece.TextureName = ParseQuoted(line);
                const size_t endQuote = line.find('"', line.find('"') + 1);

                if (endQuote != std::string::npos)
                {
                    std::istringstream coords(line.substr(endQuote + 1));
                    coords >> piece.SrcLeft >> piece.SrcTop >> piece.SrcRight >> piece.SrcBottom >> piece.DstLeft >> piece.DstTop >> piece.DstRight >> piece.DstBottom;
                    sprite.Pieces.push_back(std::move(piece));
                }
            }
            else if (key == "tcoords")
            {
                int32 index = -1;
                input >> index;

                if (index >= 0 && index < static_cast<int32>(sprite.Pieces.size()))
                {
                    auto& piece = sprite.Pieces[static_cast<size_t>(index)];

                    for (auto& coord : piece.TexCoords)
                    {
                        input >> coord.U >> coord.V;
                    }

                    piece.HasTexCoords = true;
                }
            }

            continue;
        }

        if (pendingControl && IsOpenBrace(line))
        {
            inControl = true;
            pendingControl = false;
            controlNestedDepth = 0;
            control = FUiControlDef{};
            control.Id = pendingId;
            continue;
        }

        if (inControl)
        {
            const std::string lowerLine = Lower(line);

            if (controlNestedDepth == 0 && (lowerLine == "leftbutton" || lowerLine == "rightbutton")) { pendingControlSection = lowerLine; continue; }

            if (IsOpenBrace(line))
            {
                ++controlNestedDepth;

                if (controlNestedDepth == 1 && !pendingControlSection.empty())
                {
                    activeControlSection = std::move(pendingControlSection);
                    pendingControlSection.clear();
                }

                continue;
            }

            if (IsCloseBrace(line))
            {
                if (controlNestedDepth > 0)
                {
                    --controlNestedDepth;

                    if (controlNestedDepth == 0)
                    {
                        activeControlSection.clear();
                    }

                    continue;
                }

                if (control.Id == 0)
                {
                    control.Id = -1 - static_cast<int32>(window.Controls.size());
                }

                window.Controls.push_back(std::move(control));
                inControl = false;
                continue;
            }

            if (controlNestedDepth > 0)
            {
                if (controlNestedDepth == 1 && !activeControlSection.empty())
                {
                    FUiSubButtonDef& button = activeControlSection == "leftbutton" ? control.LeftButton : control.RightButton;
                    std::istringstream input(line);
                    std::string key;
                    input >> key;
                    key = Lower(std::move(key));

                    if (key == "position")
                    {
                        input >> button.X >> button.Y;
                    }
                    else if (key == "size")
                    {
                        input >> button.W >> button.H;
                    }
                    else if (key == "checkedimage")
                    {
                        button.CheckedImage = ParseQuoted(line);
                    }
                    else if (key == "focusedimage")
                    {
                        button.FocusedImage = ParseQuoted(line);
                    }
                    else if (key == "disabledimage")
                    {
                        button.DisabledImage = ParseQuoted(line);
                    }
                    else if (key == "uncheckedimage")
                    {
                        button.UncheckedImage = ParseQuoted(line);
                    }
                }

                continue;
            }

            std::istringstream input(line);
            std::string key;
            input >> key;
            key = Lower(std::move(key));

            if (key == "classid")
            {
                input >> control.ClassId;
            }
            else if (key == "position")
            {
                input >> control.Rect.X >> control.Rect.Y;
            }
            else if (key == "size")
            {
                input >> control.Rect.W >> control.Rect.H;
            }
            else if (key == "font")
            {
                input >> control.Font;
            }
            else if (key == "textcolor")
            {
                control.TextColor = ParseColor(input, control.TextColor);
            }
            else if (key == "disabledcolor")
            {
                control.DisabledColor = ParseColor(input, control.DisabledColor);
            }
            else if (key == "focuscolor")
            {
                control.FocusColor = ParseColor(input, control.FocusColor);
            }
            else if (key == "windowtext")
            {
                input >> control.TextKey;

                if (control.TextKey.empty())
                {
                    control.TextKey = ParseQuoted(line);
                }
            }
            else if (key == "textformat")
            {
                control.TextCenter = line.find("CENTER") != std::string::npos;
            }
            else if (key == "maxsymbols")
            {
                input >> control.MaxSymbols;
            }
            else if (key == "enteredonfocus")
            {
                control.EnteredOnFocus = ParseBool(input, control.EnteredOnFocus);
            }
            else if (key == "hotkey")
            {
                control.HotKey = ParseQuoted(line);
            }
            else if (key == "hypertext")
            {
                control.HyperTextResource = ParseQuoted(line);
            }
            else if (key == "imageoffset")
            {
                input >> control.ImageOffset.X >> control.ImageOffset.Y;
            }
            else if (key == "group")
            {
                input >> control.Group;
            }
            else if (key == "checkedimage")
            {
                control.CheckedImage = ParseQuoted(line);
            }
            else if (key == "uncheckedimage")
            {
                control.UncheckedImage = ParseQuoted(line);
            }
            else if (key == "focusedimage")
            {
                control.FocusedImage = ParseQuoted(line);
            }
            else if (key == "image")
            {
                control.ImageName = ParseQuoted(line);
            }
            else if (key == "drawmethod")
            {
                control.DrawSpriteName = ParseQuoted(line);
            }
            else if (key == "windowhelp")
            {
                control.WindowHelp = ParseQuoted(line);
            }
            else if (key == "slotempty")
            {
                control.SlotEmptyImage = ParseQuoted(line);
            }
            else if (key == "slotfull")
            {
                control.SlotFullImage = ParseQuoted(line);
            }
            else if (key == "slotborder")
            {
                control.SlotBorderImage = ParseQuoted(line);
            }
            else if (key == "scrollspr")
            {
                control.ScrollSpriteName = ParseQuoted(line);
                const size_t endQuote = line.find('"', line.find('"') + 1);

                if (endQuote != std::string::npos)
                {
                    std::istringstream sizeInput(line.substr(endQuote + 1));
                    sizeInput >> control.ScrollSpriteWidth >> control.ScrollSpriteHeight;
                }
            }
            else if (key == "deltastep")
            {
                input >> control.DeltaStep;
            }
            else if (key == "range")
            {
                input >> control.RangeMin >> control.RangeMax;
            }
            else if (key == "progresspos")
            {
                input >> control.ProgressPos;
            }
            else if (key == "statusshow")
            {
                input >> control.StatusShow;
            }
            else if (key == "statuspos")
            {
                input >> control.StatusPos.X >> control.StatusPos.Y;
            }
            else if (key == "password")
            {
                control.Password = ParseBool(input, control.Password);
            }
            else if (key == "buttonstyle")
            {
                control.SendQuit = line.find("SEND_QUIT") != std::string::npos;
                control.SendHelp = line.find("SEND_HELP") != std::string::npos;
            }
            else if (key == "hidden")
            {
                control.Hidden = ParseBool(input, control.Hidden);
            }
            else if (key == "disabled")
            {
                control.Disabled = ParseBool(input, control.Disabled);
            }

            continue;
        }

        std::istringstream input(line);
        std::string key;
        input >> key;
        key = Lower(std::move(key));

        if (key == "windowname")
        {
            window.Name = ParseQuoted(line);
        }
        else if (!window.Name.empty() && key == "windowtext")
        {
            input >> window.TextKey;

            if (window.TextKey.empty())
            {
                window.TextKey = ParseQuoted(line);
            }
        }
        else if (!window.Name.empty() && key == "position")
        {
            input >> window.Rect.X >> window.Rect.Y;
        }
        else if (!window.Name.empty() && key == "size")
        {
            input >> window.Rect.W >> window.Rect.H;
        }
        else if (!window.Name.empty() && key == "font")
        {
            input >> window.Font;
        }
        else if (!window.Name.empty() && key == "textcolor")
        {
            window.TextColor = ParseColor(input, window.TextColor);
        }
        else if (!window.Name.empty() && key == "recttitle")
        {
            input >> window.TitleRect.X >> window.TitleRect.Y >> window.TitleRect.W >> window.TitleRect.H;
        }
        else if (!window.Name.empty() && key == "alignwin")
        {
            window.AlignCenterX = line.find("CENTER_X") != std::string::npos;
            window.AlignCenterY = line.find("CENTER_Y") != std::string::npos;
            window.AlignRightX = line.find("RIGHT_X") != std::string::npos;
            window.AlignRightY = line.find("RIGHT_Y") != std::string::npos;
        }
        else if (!window.Name.empty() && key == "savelastposition")
        {
            window.SaveLastPosition = ParseBool(input, window.SaveLastPosition);
        }
        else if (!window.Name.empty() && key == "candragdrop")
        {
            window.CanDragDrop = ParseBool(input, window.CanDragDrop);
        }
        else if (!window.Name.empty() && key == "cannotcross")
        {
            window.CanNotCross = ParseBool(input, window.CanNotCross);
        }
        else if (!window.Name.empty() && key == "cangotop")
        {
            window.CanGoTop = ParseBool(input, window.CanGoTop);
        }
        else if (!window.Name.empty() && key == "escapehandle")
        {
            window.EscapeHandle = ParseBool(input, window.EscapeHandle);
        }
        else if (!window.Name.empty() && key == "drawmethod")
        {
            window.DrawNone = line.find("NONE") != std::string::npos;
            window.DrawSpriteName = ParseQuoted(line);
        }
    }

    if (window.Name.empty()) { return FStatus::Error(EStatusCode::InvalidData, "UI window has no windowName: " + std::string(logicalName)); }

    return window;
}

FStatus FUiRuntime::Initialize(const FResourceManager& resources, const FUiBootstrapDesc& desc, FLogger* logger)
{
    Bootstrap = desc;
    auto strings = LoadUiStringTableFromResource(resources, Bootstrap.StringsResource);

    if (!strings.IsOk()) { return strings.Status(); }

    auto connection = LoadUiWindowFromResource(resources, Bootstrap.ConnectionWindowResource);

    if (!connection.IsOk()) { return connection.Status(); }

    auto pickPerson = LoadUiWindowFromResource(resources, Bootstrap.PickPersonWindowResource);

    if (!pickPerson.IsOk() && logger)
    {
        logger->Warning("character-select UI is not available: " + pickPerson.Status().Message());
    }

    auto createPerson = LoadUiWindowFromResource(resources, Bootstrap.CreatePersonWindowResource);

    if (!createPerson.IsOk() && logger)
    {
        logger->Warning("create-character UI is not available: " + createPerson.Status().Message());
    }

    auto deleteCharacter = LoadUiWindowFromResource(resources, Bootstrap.DeleteCharacterWindowResource);

    if (!deleteCharacter.IsOk() && logger)
    {
        logger->Warning("delete-character UI is not available: " + deleteCharacter.Status().Message());
    }

    auto connectMessage = LoadUiWindowFromResource(resources, Bootstrap.ConnectMessageWindowResource);

    if (!connectMessage.IsOk() && logger)
    {
        logger->Warning("confirmation UI is not available: " + connectMessage.Status().Message());
    }

    auto message = LoadUiWindowFromResource(resources, Bootstrap.MessageWindowResource);

    if (!message.IsOk() && logger)
    {
        logger->Warning("generic message UI is not available: " + message.Status().Message());
    }

    if (!resources.Catalog().FindByLogicalName(Bootstrap.LoginBackgroundTexture)) { return FStatus::Error(EStatusCode::NotFound, "login background texture is not cataloged: " + Bootstrap.LoginBackgroundTexture); }

    StringTable = std::move(strings.Value());
    Connection = std::move(connection.Value());

    if (pickPerson.IsOk())
    {
        PickPerson = std::move(pickPerson.Value());
    }

    if (createPerson.IsOk())
    {
        CreatePerson = std::move(createPerson.Value());
    }

    if (deleteCharacter.IsOk())
    {
        DeleteCharacter = std::move(deleteCharacter.Value());
    }

    if (connectMessage.IsOk())
    {
        ConnectMessage = std::move(connectMessage.Value());
    }

    if (message.IsOk())
    {
        Message = std::move(message.Value());
    }

    Ready = true;
    AddStatusLine("ui: connection window loaded, controls=" + std::to_string(Connection.Controls.size()) + ", sprites=" + std::to_string(Connection.Sprites.size()));

    if (!PickPerson.Name.empty())
    {
        AddStatusLine("ui: character window loaded, controls=" + std::to_string(PickPerson.Controls.size()) + ", sprites=" + std::to_string(PickPerson.Sprites.size()));
    }

    if (!CreatePerson.Name.empty())
    {
        AddStatusLine("ui: create window loaded, controls=" + std::to_string(CreatePerson.Controls.size()));
    }

    if (!DeleteCharacter.Name.empty())
    {
        AddStatusLine("ui: delete window loaded, controls=" + std::to_string(DeleteCharacter.Controls.size()));
    }

    if (!ConnectMessage.Name.empty())
    {
        AddStatusLine("ui: confirm window loaded, controls=" + std::to_string(ConnectMessage.Controls.size()));
    }

    if (!Message.Name.empty())
    {
        AddStatusLine("ui: message window loaded, controls=" + std::to_string(Message.Controls.size()));
    }

    if (logger)
    {
        logger->Info("UI runtime initialized: strings=" + std::to_string(StringTable.size()) + ", connection=" + Connection.Name + ", controls=" + std::to_string(Connection.Controls.size()) + ", pick_person=" + PickPerson.Name + ", create_person=" + CreatePerson.Name + ", connect_message=" + ConnectMessage.Name + ", message=" + Message.Name);
    }

    return FStatus::Ok();
}

void FUiRuntime::SetStage(std::string stage, float progress)
{
    CurrentStage = std::move(stage);
    CurrentProgress = std::clamp(progress, 0.0f, 1.0f);
}

void FUiRuntime::AddStatusLine(std::string line)
{
    Status.push_back(std::move(line));

    if (Status.size() > 6)
    {
        Status.erase(Status.begin());
    }
}

std::string FUiRuntime::ResolveText(std::string_view key) const
{
    auto it = StringTable.find(std::string(key));

    if (it == StringTable.end()) { return {}; }

    return it->second;
}

std::string FUiRuntime::ConsumeLastAction()
{
    std::string action = std::move(Actions.LastAction);
    Actions.LastAction.clear();
    return action;
}

const FUiWindowDef& FUiRuntime::ActiveModalWindow() const
{
    if ((Modal == EUiModalDialog::CharacterCreate || Modal == EUiModalDialog::CharacterExit) && !Message.Name.empty()) { return Message; }

    if (Modal == EUiModalDialog::CharacterDelete && !DeleteCharacter.Name.empty()) { return DeleteCharacter; }

    return ConnectMessage.Name.empty() ? Connection : ConnectMessage;
}

void FUiRuntime::SetLoginCredentials(std::string login, std::string password, bool saveLogin)
{
    Actions.LoginText = std::move(login);
    Actions.PasswordText = std::move(password);
    Actions.SaveLogin = saveLogin;
}

void FUiRuntime::ShowExitConfirmation()
{
    Modal = EUiModalDialog::CharacterExit;
    ModalText = Bootstrap.Lang == 1 ? "Exit to login screen?" : "Выйти на экран логина?";
    ModalEditText.clear();
    Actions.LastAction = "character_exit_dialog";
}

void FUiRuntime::ShowCreateConfirmation()
{
    Modal = EUiModalDialog::CharacterCreate;
    ModalText = Bootstrap.Lang == 1 ? "Create this character?" : "Создать этого персонажа?";
    ModalEditText.clear();
    Actions.LastAction = "character_create_dialog";
}

void FUiRuntime::ShowDeleteConfirmation()
{
    Modal = EUiModalDialog::CharacterDelete;
    const std::string name = WideToUtf8(SelectedCharacterName());
    ModalText = Bootstrap.Lang == 1 ? "Type character name to delete: " + name : "Введите имя персонажа для удаления: " + name;
    ModalEditText.clear();
    Actions.FocusedControlId = 4;
    Actions.LastAction = "character_delete_dialog";
}

void FUiRuntime::DismissModal()
{
    Modal = EUiModalDialog::None;
    ModalText.clear();
    ModalEditText.clear();
    Actions.PressedControlId = 0;
}

void FUiRuntime::ApplyCharacterDeleted(int32 slot)
{
    const int32 index = std::clamp(slot, 0, 2);
    FCharacterSlotInfo& info = CharacterSlotState[static_cast<size_t>(index)];
    info = FCharacterSlotInfo{};
    info.Slot = index;
    info.Present = false;
    info.CanCreate = true;
    CharacterNameEdits[static_cast<size_t>(index)] = DefaultCharacterNameForSlot(index);
    SelectedSlot = index;
    ActiveCharacterEditId = 60 + index;
    SyncCharacterSelectControls();
}

void FUiRuntime::ApplyCharacterCreated(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance)
{
    const int32 index = std::clamp(slot, 0, 2);
    FCharacterSlotInfo& info = CharacterSlotState[static_cast<size_t>(index)];
    info = FCharacterSlotInfo{};
    info.Slot = index;
    info.Present = true;
    info.CanCreate = false;
    info.Name = name;
    info.Female = appearance.Female;
    info.Face = appearance.Face;
    info.Hair = appearance.Hair;
    info.HairColor = appearance.HairColor;
    info.Tattoo = appearance.Tattoo;
    info.Strength = Appearance.Strength;
    info.Dexterity = Appearance.Dexterity;
    info.Accuracy = Appearance.Accuracy;
    info.Endurance = Appearance.Endurance;
    info.Fire = Appearance.Fire;
    info.Water = Appearance.Water;
    info.Earth = Appearance.Earth;
    info.Air = Appearance.Air;
    info.MaxHp = 100;
    info.CurrentHp = 100;
    info.MaxMp = 100;
    info.CurrentMp = 100;
    info.MaxSatiety = 100;
    info.CurrentSatiety = 100;
    info.TitleId = 14;
    info.DegreeId = 112;
    info.TitleLevel = 1;
    info.DegreeLevel = 1;
    info.TitleXp = 0;
    info.DegreeXp = 0;
    info.TitleNextXp = 50;
    info.DegreeNextXp = 50;
    info.Karma = 3;
    CharacterNameEdits[static_cast<size_t>(index)] = name;
    SelectedSlot = index;
    ActiveCharacterEditId = 0;
    SyncCharacterSelectControls();
}

FUiRectF FUiRuntime::BuildDesignRect(const tagRECT& clientRect) const
{
    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    const float clientW = static_cast<float>(width);
    const float clientH = static_cast<float>(height);
    const float designW = static_cast<float>(Bootstrap.DesignWidth > 0 ? Bootstrap.DesignWidth : 1024);
    const float designH = static_cast<float>(Bootstrap.DesignHeight > 0 ? Bootstrap.DesignHeight : 768);
    return FUiRectF
    {
        std::floor((clientW - designW) * 0.5f), std::floor((clientH - designH) * 0.5f), designW, designH
    };
}

FUiRectF FUiRuntime::BuildConnectionRect(const tagRECT& clientRect) const
{
    FUiRectF design = BuildDesignRect(clientRect);
    const float w = static_cast<float>(Connection.Rect.W);
    const float h = static_cast<float>(Connection.Rect.H);
    return FUiRectF
    {
        std::floor(design.X + (design.W - w) * 0.5f), std::floor(design.Y + (design.H - h) * 0.5f), w, h
    };
}

FUiRectF FUiRuntime::BuildWindowRect(const FUiWindowDef& window, const tagRECT& clientRect) const
{
    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    const float clientW = static_cast<float>(width);
    const float clientH = static_cast<float>(height);
    float x = static_cast<float>(window.Rect.X);
    float y = static_cast<float>(window.Rect.Y);
    const float w = static_cast<float>(window.Rect.W);
    const float h = static_cast<float>(window.Rect.H);

    if (window.AlignRightX)
    {
        x = clientW - w + x;
    }
    else if (window.AlignCenterX)
    {
        x = (clientW - w) * 0.5f + x;
    }

    if (window.AlignRightY)
    {
        y = clientH - h + y;
    }
    else if (window.AlignCenterY)
    {
        y = (clientH - h) * 0.5f + y;
    }

    return FUiRectF
    {
        std::floor(x), std::floor(y), w, h
    };
}

const FUiControlDef* FUiRuntime::HitTestConnection(int32 x, int32 y, const tagRECT& clientRect) const
{
    if (!Ready) { return nullptr; }

    FUiRectF wr = BuildConnectionRect(clientRect);
    const float scale = wr.W / static_cast<float>(std::max(1, Connection.Rect.W));

    for (auto it = Connection.Controls.rbegin(); it != Connection.Controls.rend(); ++it)
    {
        const FUiControlDef& control = *it;

        if (!ControlCanReceiveMouse(control)) { continue; }

        FUiRectF r
        {
            wr.X + control.Rect.X * scale, wr.Y + control.Rect.Y * scale, control.Rect.W * scale, control.Rect.H * scale
        };

        if (static_cast<float>(x) >= r.X && static_cast<float>(y) >= r.Y && static_cast<float>(x) < r.X + r.W && static_cast<float>(y) < r.Y + r.H) { return &control; }
    }

    return nullptr;
}

const FUiControlDef* FUiRuntime::HitTestCharacterSelect(int32 x, int32 y, const tagRECT& clientRect) const
{
    if (!Ready || PickPerson.Name.empty()) { return nullptr; }

    FUiRectF wr = BuildWindowRect(PickPerson, clientRect);

    for (auto it = PickPerson.Controls.rbegin(); it != PickPerson.Controls.rend(); ++it)
    {
        const FUiControlDef& control = *it;

        if (!ControlCanReceiveMouse(control)) { continue; }

        FUiRectF r
        {
            wr.X + static_cast<float>(control.Rect.X), wr.Y + static_cast<float>(control.Rect.Y), static_cast<float>(std::max(1, control.Rect.W)), static_cast<float>(std::max(1, control.Rect.H))
        };

        if (EqualsNoCase(control.ClassId, "SPINBUTTON") && (control.Rect.W <= 0 || control.Rect.H <= 0))
        {
            r.W = 37.0f;
            r.H = 26.0f;
        }

        if (static_cast<float>(x) >= r.X && static_cast<float>(y) >= r.Y && static_cast<float>(x) < r.X + r.W && static_cast<float>(y) < r.Y + r.H) { return &control; }
    }

    return nullptr;
}

const FUiControlDef* FUiRuntime::HitTestModal(int32 x, int32 y, const tagRECT& clientRect) const
{
    if (!Ready || Modal == EUiModalDialog::None) { return nullptr; }

    const FUiWindowDef& window = ActiveModalWindow();

    if (window.Name.empty()) { return nullptr; }

    FUiRectF wr = BuildWindowRect(window, clientRect);

    for (auto it = window.Controls.rbegin(); it != window.Controls.rend(); ++it)
    {
        const FUiControlDef& control = *it;

        if (!ControlCanReceiveMouse(control)) { continue; }

        FUiRectF r
        {
            wr.X + static_cast<float>(control.Rect.X), wr.Y + static_cast<float>(control.Rect.Y), static_cast<float>(std::max(1, control.Rect.W)), static_cast<float>(std::max(1, control.Rect.H))
        };

        if (static_cast<float>(x) >= r.X && static_cast<float>(y) >= r.Y && static_cast<float>(x) < r.X + r.W && static_cast<float>(y) < r.Y + r.H) { return &control; }
    }

    return nullptr;
}

bool FUiRuntime::IsEditControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "EDIT"); }
bool FUiRuntime::IsCheckControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "CHECKBOX") || control.Id == SavePasswordId; }
bool FUiRuntime::IsButtonControl(const FUiControlDef& control) const { return EqualsNoCase(control.ClassId, "BUTTON"); }

int32 FUiRuntime::CharacterFocusForControl(int32 controlId) const
{
    if (controlId >= 12 && controlId <= 16) { return controlId; }

    if (controlId >= 7 && controlId <= 11) { return controlId + 5; }

    return 0;
}

bool FUiRuntime::PointInsidePickPersonWindow(int32 x, int32 y, const tagRECT& clientRect) const
{
    if (PickPerson.Name.empty()) { return false; }

    const FUiRectF wr = BuildWindowRect(PickPerson, clientRect);
    return static_cast<float>(x) >= wr.X && static_cast<float>(y) >= wr.Y && static_cast<float>(x) < wr.X + wr.W && static_cast<float>(y) < wr.Y + wr.H;
}

int32 FUiRuntime::CharacterSpinDeltaForPoint(const FUiControlDef& control, int32 x, int32 y, const tagRECT& clientRect) const
{
    FUiRectF wr = BuildWindowRect(PickPerson, clientRect);
    const float controlX = wr.X + static_cast<float>(control.Rect.X);
    const float controlY = wr.Y + static_cast<float>(control.Rect.Y);
    auto contains = [&](const FUiSubButtonDef& button)
    {
        if (button.W <= 0 || button.H <= 0) { return false; }

        const float bx = controlX + static_cast<float>(button.X);
        const float by = controlY + static_cast<float>(button.Y);
        return static_cast<float>(x) >= bx && static_cast<float>(y) >= by && static_cast<float>(x) < bx + static_cast<float>(button.W) && static_cast<float>(y) < by + static_cast<float>(button.H);
    };

    if (contains(control.RightButton)) { return -1; }

    if (contains(control.LeftButton)) { return 1; }

    const float width = static_cast<float>(std::max(37, control.Rect.W));
    return static_cast<float>(x) < controlX + width * 0.5f ? -1 : 1;
}

void FUiRuntime::ActivateControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;

    if (IsEditControl(control)) { Actions.FocusedControlId = control.Id; Actions.LastAction = control.Id == PasswordEditId || control.Password ? "focus_password" : "focus_login"; return; }

    if (IsCheckControl(control)) { Actions.SaveLogin = !Actions.SaveLogin; Actions.LastAction = Actions.SaveLogin ? "save_login_on" : "save_login_off"; return; }

    if (IsButtonControl(control))
    {
        if (control.Id == LoginButtonId)
        {
            Actions.LastAction = "login_requested";
        }
        else if (control.Id == CancelButtonId || control.Id == QuitButtonId || control.SendQuit)
        {
            Actions.LastAction = "quit_requested";
        }
        else if (control.Id == RegistrationButtonId)
        {
            Actions.LastAction = "registration_requested";
        }
        else
        {
            Actions.LastAction = "click_control_" + std::to_string(control.Id);
        }

        if (logger)
        {
            logger->Info("UI action: " + Actions.LastAction);
        }
    }
}

void FUiRuntime::ActivateCharacterControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;

    if (control.Id >= 63 && control.Id <= 65)
    {
        SelectedSlot = std::clamp(control.Id - 63, 0, 2);
        ActiveCharacterEditId = 60 + SelectedSlot;
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
        Actions.LastAction = "character_slot_" + std::to_string(SelectedSlot);
    }
    else if (control.Id >= 60 && control.Id <= 62)
    {
        SelectedSlot = std::clamp(control.Id - 60, 0, 2);
        ActiveCharacterEditId = control.Id;
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
        Actions.LastAction = "character_edit_" + std::to_string(SelectedSlot);
    }
    else if (control.Id >= 12 && control.Id <= 16)
    {
        const int32 delta = CharacterSpinDelta < 0 ? -1 : 1;
        auto cycle = [delta](int32 value, int32 count)
        {
            if (count <= 0)
            {
                return 0;
            }

            value = (value + delta) % count;
            return value < 0 ? value + count : value;
        };

        if (control.Id == 12)
        {
            Appearance.Gender = cycle(Appearance.Gender, 2);
            Appearance.Face = 0;
            Appearance.Hair = 0;
            Appearance.HairColor = 0;
            Appearance.Tattoo = 0;
        }
        else if (control.Id == 13)
        {
            Appearance.Face = cycle(Appearance.Face, CharacterAppearanceOptionCount(13));
        }
        else if (control.Id == 14)
        {
            Appearance.Hair = cycle(Appearance.Hair, CharacterAppearanceOptionCount(14));
        }
        else if (control.Id == 15)
        {
            Appearance.HairColor = cycle(Appearance.HairColor, CharacterAppearanceOptionCount(15));
        }
        else if (control.Id == 16)
        {
            Appearance.Tattoo = cycle(Appearance.Tattoo, CharacterAppearanceOptionCount(16));
        }

        ClampCharacterAppearance();
        SyncCharacterSelectControls();
        Actions.LastAction = "character_appearance_changed";
    }
    else if (control.Id == CharacterContinueButtonId)
    {
        if (SelectedCharacterPresent())
        {
            Actions.LastAction = "character_enter_requested";
        }
        else
        {
            ShowCreateConfirmation();
        }
    }
    else if (control.Id == CharacterExitButtonId || control.SendQuit)
    {
        ShowExitConfirmation();
    }
    else if (control.Id == CharacterDeleteButtonId)
    {
        ShowDeleteConfirmation();
    }
    else
    {
        Actions.LastAction = "character_click_" + std::to_string(control.Id);
    }

    if (logger)
    {
        logger->Info("UI action: " + Actions.LastAction);
    }
}

void FUiRuntime::ActivateModalControl(const FUiControlDef& control, FLogger* logger)
{
    Actions.LastControlId = control.Id;
    const EUiModalDialog current = Modal;

    if (IsEditControl(control)) { Actions.FocusedControlId = control.Id; Actions.LastAction.clear(); return; }

    if (current == EUiModalDialog::CharacterCreate)
    {
        if (control.Id == 1)
        {
            DismissModal();
            Actions.LastAction = "character_create_confirmed";
        }
        else if (control.Id == 2 || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_create_cancelled";
        }
        else
        {
            Actions.LastAction.clear();
        }
    }
    else if (current == EUiModalDialog::CharacterDelete)
    {
        if (control.Id == 5 && ModalEditMatchesSelectedCharacter())
        {
            DismissModal();
            Actions.LastAction = "character_delete_confirmed";
        }
        else if (control.Id == 1 || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_delete_cancelled";
        }
        else
        {
            Actions.LastAction = "character_delete_name_required";
        }
    }
    else if (current == EUiModalDialog::CharacterExit)
    {
        if (control.Id == 1)
        {
            DismissModal();
            Actions.LastAction = "character_back_confirmed";
        }
        else if (control.Id == 2 || control.SendQuit)
        {
            DismissModal();
            Actions.LastAction = "character_back_cancelled";
        }
        else
        {
            Actions.LastAction.clear();
        }
    }
    else
    {
        DismissModal();
        Actions.LastAction = "modal_closed";
    }

    if (logger && !Actions.LastAction.empty())
    {
        logger->Info("UI action: " + Actions.LastAction);
    }
}

bool FUiRuntime::HandleInputFrame(const FInputSnapshot& input, const tagRECT& clientRect, FLogger* logger)
{
    if (!Ready) { return false; }

    bool changed = false;

    if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        SyncCharacterSelectControls();
    }

    const FUiControlDef* hovered = nullptr;

    if (Modal != EUiModalDialog::None)
    {
        hovered = HitTestModal(input.MouseX, input.MouseY, clientRect);
    }
    else
    {
        hovered = CurrentMode == EUiRuntimeMode::CharacterSelect ? HitTestCharacterSelect(input.MouseX, input.MouseY, clientRect) : HitTestConnection(input.MouseX, input.MouseY, clientRect);
    }

    const int32 newHover = hovered ? hovered->Id : 0;
    int32 spinHoverDirection = 0;

    if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && EqualsNoCase(hovered->ClassId, "SPINBUTTON"))
    {
        spinHoverDirection = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect);
    }

    if (newHover != Actions.HoverControlId || spinHoverDirection != Actions.SpinHoverDirection)
    {
        Actions.HoverControlId = newHover;
        Actions.SpinHoverDirection = spinHoverDirection;
        changed = true;
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && SceneRotateDragActive && input.LeftButton)
    {
        const int32 dx = input.MouseX - SceneRotateLastX;
        SceneRotateLastX = input.MouseX;

        if (dx != 0)
        {
            SceneAngle += static_cast<float>(dx) * 0.01f;
            changed = true;
        }
    }

    if (input.LeftPressed)
    {
        Actions.PressedControlId = newHover;
        Actions.SpinPressedDirection = 0;
        SceneRotateDragActive = false;

        if (hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && EqualsNoCase(hovered->ClassId, "SPINBUTTON"))
        {
            CharacterSpinDelta = CharacterSpinDeltaForPoint(*hovered, input.MouseX, input.MouseY, clientRect);
            Actions.SpinPressedDirection = CharacterSpinDelta;
            const int32 focus = CharacterFocusForControl(hovered->Id);

            if (focus != 0)
            {
                SceneCameraFocusId = focus;
            }
        }
        else if (!hovered && CurrentMode == EUiRuntimeMode::CharacterSelect && Modal == EUiModalDialog::None && !PointInsidePickPersonWindow(input.MouseX, input.MouseY, clientRect))
        {
            SceneRotateDragActive = true;
            SceneRotateLastX = input.MouseX;
        }

        changed = true;
    }

    if (input.LeftReleased)
    {
        const int32 pressed = Actions.PressedControlId;
        Actions.PressedControlId = 0;
        Actions.SpinPressedDirection = 0;
        SceneRotateDragActive = false;
        changed = true;

        if (hovered && hovered->Id != 0 && hovered->Id == pressed)
        {
            if (Modal != EUiModalDialog::None)
            {
                ActivateModalControl(*hovered, logger);
            }
            else if (CurrentMode == EUiRuntimeMode::CharacterSelect)
            {
                ActivateCharacterControl(*hovered, logger);
            }
            else
            {
                ActivateControl(*hovered, logger);
            }
        }
    }

    if (Modal != EUiModalDialog::None)
    {
        if (Modal == EUiModalDialog::CharacterDelete && Actions.FocusedControlId == 4)
        {
            if (input.BackspacePressed && !ModalEditText.empty())
            {
                RemoveLastUtf8Codepoint(ModalEditText);
                changed = true;
            }

            if (!input.TypedText.empty())
            {
                AppendUtf8Limited(ModalEditText, input.TypedText, 20);
                changed = true;
            }
        }

        if (input.EnterPressed)
        {
            const EUiModalDialog current = Modal;

            if (current == EUiModalDialog::CharacterDelete && !ModalEditMatchesSelectedCharacter())
            {
                Actions.LastAction = "character_delete_name_required";
            }
            else
            {
                DismissModal();
                Actions.LastAction = current == EUiModalDialog::CharacterCreate ? "character_create_confirmed" : current == EUiModalDialog::CharacterDelete ? "character_delete_confirmed" : "character_back_confirmed";
            }

            changed = true;
        }

        return changed;
    }

    if (CurrentMode == EUiRuntimeMode::Login && (Actions.FocusedControlId == LoginEditId || Actions.FocusedControlId == PasswordEditId))
    {
        std::string& target = Actions.FocusedControlId == PasswordEditId ? Actions.PasswordText : Actions.LoginText;

        if (input.BackspacePressed && !target.empty())
        {
            RemoveLastUtf8Codepoint(target);
            changed = true;
        }

        if (!input.TypedText.empty())
        {
            const size_t limit = Actions.FocusedControlId == PasswordEditId ? MaxPasswordChars : MaxLoginChars;
            AppendUtf8Limited(target, input.TypedText, limit);
            changed = true;
        }

        if (input.TabPressed)
        {
            Actions.FocusedControlId = Actions.FocusedControlId == LoginEditId ? PasswordEditId : LoginEditId;
            changed = true;
        }
    }

    if (CurrentMode == EUiRuntimeMode::CharacterSelect && ActiveCharacterEditId >= 60 && ActiveCharacterEditId <= 62 && !CharacterActionLocked)
    {
        std::wstring& target = CharacterNameEdits[static_cast<size_t>(ActiveCharacterEditId - 60)];

        if (input.BackspacePressed && !target.empty())
        {
            target.pop_back();
            changed = true;
        }

        const std::wstring typed = Utf8ToWideLocal(input.TypedText);

        for (wchar_t ch : typed)
        {
            if (target.size() < MaxCharacterNameChars && IsCharacterNameChar(ch))
            {
                target.push_back(ch);
                changed = true;
            }
        }
    }

    if (CurrentMode == EUiRuntimeMode::Game)
    {
        if (input.BackspacePressed && !GameChat.empty())
        {
            RemoveLastUtf8Codepoint(GameChat);
            changed = true;
        }

        if (!input.TypedText.empty())
        {
            GameChat += input.TypedText;
            GameChat = TrimUtf8ToCodepoints(GameChat, 240);
            changed = true;
        }
    }

    if (input.EnterPressed)
    {
        if (CurrentMode == EUiRuntimeMode::CharacterSelect && !SelectedCharacterPresent() && SelectedCharacterCanCreate())
        {
            ShowCreateConfirmation();
        }
        else if (CurrentMode == EUiRuntimeMode::Game)
        {
            Actions.LastAction = "game_chat_enter";
        }
        else
        {
            Actions.LastAction = CurrentMode == EUiRuntimeMode::CharacterSelect ? "character_enter_requested" : "login_requested";
        }

        changed = true;

        if (logger)
        {
            logger->Info("UI action: " + Actions.LastAction);
        }
    }

    return changed;
}

void FUiRuntime::SetMode(EUiRuntimeMode mode)
{
    CurrentMode = mode;
    Actions.HoverControlId = 0;
    Actions.PressedControlId = 0;
    Actions.LastAction.clear();
    Modal = EUiModalDialog::None;
    ModalText.clear();
    SceneRotateDragActive = false;
    Actions.SpinHoverDirection = 0;
    Actions.SpinPressedDirection = 0;

    if (CurrentMode == EUiRuntimeMode::CharacterSelect)
    {
        SceneCameraFocusId = 0;
        SyncCharacterSelectControls();
    }
}

std::wstring FUiRuntime::DefaultCharacterNameForSlot(int32 slot) const
{
    std::wstring name;

    for (wchar_t ch : Utf8ToWideLocal(Actions.LoginText))
    {
        if (IsCharacterNameChar(ch))
        {
            name.push_back(ch);
        }

        if (name.size() >= 12)
        {
            break;
        }
    }

    if (name.empty())
    {
        name = L"Hero";
    }

    if (!name.empty() && name.front() >= L'0' && name.front() <= L'9')
    {
        name.insert(name.begin(), L'C');
    }

    if (slot > 0 && name.size() < 12)
    {
        name.push_back(static_cast<wchar_t>(L'1' + slot));
    }

    if (name.size() > 12)
    {
        name.resize(12);
    }

    return name;
}

void FUiRuntime::SetCharacterAppearanceRules(const FCharacterAppearanceRules& rules)
{
    AppearanceRules = rules;
    ClampCharacterAppearance();
    SyncCharacterSelectControls();
}

int32 FUiRuntime::CharacterAppearanceOptionCount(int32 controlId) const
{
    const bool female = Appearance.Gender != 0;

    switch (controlId)
    {
    case 12: return 2;
    case 13: return female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount;
    case 14: return female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount;
    case 15: return AppearanceRules.HairColorCount;
    case 16: return AppearanceRules.TattooCount;
    default: return 1;
    }
}

void FUiRuntime::ClampCharacterAppearance()
{
    Appearance.Gender = ClampIndexToCount(Appearance.Gender, 2);
    Appearance.Face = ClampIndexToCount(Appearance.Face, CharacterAppearanceOptionCount(13));
    Appearance.Hair = ClampIndexToCount(Appearance.Hair, CharacterAppearanceOptionCount(14));
    Appearance.HairColor = ClampIndexToCount(Appearance.HairColor, CharacterAppearanceOptionCount(15));
    Appearance.Tattoo = ClampIndexToCount(Appearance.Tattoo, CharacterAppearanceOptionCount(16));
}

void FUiRuntime::SetCharacterSlots(const std::array<FCharacterSlotInfo, 3>& slots)
{
    CharacterSlotState = slots;
    int32 firstSelectable = 0;
    bool found = false;

    for (int32 i = 0; i < 3; ++i)
    {
        if (CharacterSlotState[static_cast<size_t>(i)].Present)
        {
            firstSelectable = i;
            found = true;
            break;
        }
    }

    if (!found)
    {
        for (int32 i = 0; i < 3; ++i)
        {
            if (CharacterSlotState[static_cast<size_t>(i)].CanCreate)
            {
                firstSelectable = i;
                found = true;
                break;
            }
        }
    }

    SelectedSlot = std::clamp(found ? firstSelectable : SelectedSlot, 0, 2);

    for (int32 i = 0; i < 3; ++i)
    {
        const auto& slot = CharacterSlotState[static_cast<size_t>(i)];
        CharacterNameEdits[static_cast<size_t>(i)] = slot.Present ? slot.Name : DefaultCharacterNameForSlot(i);
    }

    ActiveCharacterEditId = 60 + SelectedSlot;
    ClampCharacterAppearance();
    SyncCharacterSelectControls();
}

void FUiRuntime::SetCharacterActionLocked(bool locked)
{
    CharacterActionLocked = locked;
    SyncCharacterSelectControls();
}

FUiControlDef* FUiRuntime::MutableCharacterControlById(int32 id)
{
    for (auto& control : PickPerson.Controls)
    {
        if (control.Id == id)
        {
            return &control;
        }
    }

    return nullptr;
}

const FUiControlDef* FUiRuntime::CharacterControlById(int32 id) const
{
    for (const auto& control : PickPerson.Controls)
    {
        if (control.Id == id)
        {
            return &control;
        }
    }

    return nullptr;
}

void FUiRuntime::SyncCharacterSelectControls()
{
    if (PickPerson.Name.empty()) { return; }

    const int32 selected = std::clamp(SelectedSlot, 0, 2);

    for (int32 i = 0; i < 3; ++i)
    {
        const auto& slot = CharacterSlotState[static_cast<size_t>(i)];

        if (auto* edit = MutableCharacterControlById(60 + i))
        {
            edit->Hidden = i != selected || slot.Present || !slot.CanCreate;
            edit->Disabled = edit->Hidden || CharacterActionLocked;
        }

        if (auto* radio = MutableCharacterControlById(63 + i))
        {
            radio->Hidden = false;
            radio->Disabled = CharacterActionLocked || (!slot.Present && !slot.CanCreate);
        }
    }

    const auto& selectedSlot = CharacterSlotState[static_cast<size_t>(selected)];
    const bool canEditAppearance = !selectedSlot.Present && selectedSlot.CanCreate && !CharacterActionLocked;

    for (int32 id = 12; id <= 16; ++id)
    {
        if (auto* control = MutableCharacterControlById(id))
        {
            control->Hidden = false;
            control->Disabled = !canEditAppearance;
        }
    }

    if (auto* del = MutableCharacterControlById(CharacterDeleteButtonId))
    {
        del->Disabled = CharacterActionLocked || !selectedSlot.Present;
    }

    if (auto* exit = MutableCharacterControlById(CharacterExitButtonId))
    {
        exit->Disabled = CharacterActionLocked;
    }

    if (auto* cont = MutableCharacterControlById(CharacterContinueButtonId))
    {
        cont->Disabled = CharacterActionLocked || (!selectedSlot.Present && !selectedSlot.CanCreate);
    }

    if (!selectedSlot.Present && selectedSlot.CanCreate && !CharacterActionLocked)
    {
        ActiveCharacterEditId = 60 + selected;
    }
}

bool FUiRuntime::SelectedCharacterPresent() const
{
    return CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))].Present;
}

bool FUiRuntime::SelectedCharacterCanCreate() const
{
    return CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))].CanCreate;
}

std::wstring FUiRuntime::SelectedCharacterName() const
{
    const int32 slot = std::clamp(SelectedSlot, 0, 2);
    const auto& info = CharacterSlotState[static_cast<size_t>(slot)];
    return info.Present ? info.Name : CharacterNameEdits[static_cast<size_t>(slot)];
}

FCharacterCreationAppearance FUiRuntime::SelectedCharacterAppearance(const FCharacterAppearanceRules& rules) const
{
    FCharacterCreationAppearance out;
    out.ModelBase = rules.ModelBase;
    out.Female = Appearance.Gender != 0;
    out.Face = ClampIndexToCount(Appearance.Face, out.Female ? rules.FemaleFaceCount : rules.MaleFaceCount);
    out.Hair = ClampIndexToCount(Appearance.Hair, out.Female ? rules.FemaleHairCount : rules.MaleHairCount);
    out.HairColor = ClampIndexToCount(Appearance.HairColor, rules.HairColorCount);
    out.Tattoo = ClampIndexToCount(Appearance.Tattoo, rules.TattooCount);
    return out;
}

FCharacterCreationAppearance FUiRuntime::SelectedCharacterSceneAppearance() const
{
    const int32 slot = std::clamp(SelectedSlot, 0, 2);
    const auto& info = CharacterSlotState[static_cast<size_t>(slot)];
    FCharacterCreationAppearance out;
    out.ModelBase = AppearanceRules.ModelBase;

    if (info.Present)
    {
        out.Female = info.Female;
        out.Face = ClampIndexToCount(info.Face, info.Female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount);
        out.Hair = ClampIndexToCount(info.Hair, info.Female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount);
        out.HairColor = ClampIndexToCount(info.HairColor, AppearanceRules.HairColorCount);
        out.Tattoo = ClampIndexToCount(info.Tattoo, AppearanceRules.TattooCount);
    }
    else
    {
        out.Female = Appearance.Gender != 0;
        out.Face = ClampIndexToCount(Appearance.Face, out.Female ? AppearanceRules.FemaleFaceCount : AppearanceRules.MaleFaceCount);
        out.Hair = ClampIndexToCount(Appearance.Hair, out.Female ? AppearanceRules.FemaleHairCount : AppearanceRules.MaleHairCount);
        out.HairColor = ClampIndexToCount(Appearance.HairColor, AppearanceRules.HairColorCount);
        out.Tattoo = ClampIndexToCount(Appearance.Tattoo, AppearanceRules.TattooCount);
    }

    return out;
}

std::string FUiRuntime::EmptyCharacterSlotText() const
{
    if (Bootstrap.Lang == 1) { return "Empty"; }

    if (Bootstrap.Lang == 3) { return "vazio"; }

    return "Пусто";
}

std::string FUiRuntime::CharacterGenderText(bool female) const
{
    if (female) { return Bootstrap.Lang == 1 ? "Female" : "Жен."; }

    std::string text = ResolveText("UISTR_WT_CPS06");
    return text.empty() ? (Bootstrap.Lang == 1 ? "Male" : "Муж.") : text;
}

int32 FUiRuntime::CharacterTextValue(int32 id) const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];
    const bool occupied = slot.Present;

    switch (id)
    {
    case 17: return occupied ? slot.Strength : Appearance.Strength;
    case 18: return occupied ? slot.Dexterity : Appearance.Dexterity;
    case 19: return occupied ? slot.Accuracy : Appearance.Accuracy;
    case 20: return occupied ? slot.Endurance : Appearance.Endurance;
    case 29: return occupied ? slot.Fire : Appearance.Fire;
    case 30: return occupied ? slot.Water : Appearance.Water;
    case 31: return occupied ? slot.Earth : Appearance.Earth;
    case 32: return occupied ? slot.Air : Appearance.Air;
    case 48: return occupied ? slot.PhysicalAttack : 0;
    case 49: return occupied ? slot.PhysicalDefense : 0;
    case 50: return occupied ? slot.TitleStats : 0;
    case 51: return occupied ? slot.MagicalAttack : 0;
    case 52: return occupied ? slot.MagicalDefense : 0;
    case 53: return occupied ? slot.DegreeStats : 0;
    default: return 0;
    }
}

float FUiRuntime::CharacterProgressRatio(int32 controlId) const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];
    const bool occupied = slot.Present;
    auto ratio = [](int32 current, int32 maximum)
    {
        return maximum <= 0 ? 0.0f : std::clamp(static_cast<float>(current) / static_cast<float>(maximum), 0.0f, 1.0f);
    };

    if (!occupied) { return controlId == 41 || controlId == 42 ? 0.0f : 1.0f; }

    if (controlId == 41) { return ratio(slot.TitleXp, slot.TitleNextXp); }

    if (controlId == 42) { return ratio(slot.DegreeXp, slot.DegreeNextXp); }

    if (controlId == 46) { return ratio(slot.CurrentMp, slot.MaxMp); }

    if (controlId == 47) { return ratio(slot.CurrentSatiety, slot.MaxSatiety); }

    if (controlId == 45) { return ratio(slot.CurrentHp, slot.MaxHp); }

    return 1.0f;
}

bool FUiRuntime::ModalEditMatchesSelectedCharacter() const
{
    if (Modal != EUiModalDialog::CharacterDelete) { return true; }

    return Utf8EqualsWideNoCase(ModalEditText, SelectedCharacterName());
}

std::string FUiRuntime::CharacterTitleText() const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];

    if (!slot.Present) { return {}; }

    const char* name = nullptr;

    switch (slot.TitleId)
    {
    case 14: name = "странник";
        break;
    default: break;
    }

    std::string title = name ? std::string(name) : (Bootstrap.Lang == 1 ? "title " + std::to_string(slot.TitleId) : "звание " + std::to_string(slot.TitleId));
    return title + " (" + std::to_string(std::max(1, slot.TitleLevel)) + ")";
}

std::string FUiRuntime::CharacterDegreeText() const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];

    if (!slot.Present) { return {}; }

    const char* name = nullptr;

    switch (slot.DegreeId)
    {
    case 112: name = "неучёный";
        break;
    default: break;
    }

    std::string degree = name ? std::string(name) : (Bootstrap.Lang == 1 ? "degree " + std::to_string(slot.DegreeId) : "ремесло " + std::to_string(slot.DegreeId));
    return degree + " (" + std::to_string(std::max(1, slot.DegreeLevel)) + ")";
}

std::string FUiRuntime::CharacterKarmaText() const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];

    if (!slot.Present) { return {}; }

    const int32 karma = slot.Karma <= 0 ? 3 : slot.Karma;

    if (Bootstrap.Lang == 1)
    {
        switch (karma)
        {
        case 1: return "Karma: very bad";
        case 2: return "Karma: bad";
        case 4: return "Karma: good";
        case 5: return "Karma: benign";
        default: return "Karma: neutral";
        }
    }

    switch (karma)
    {
    case 1: return "Карма: очень плохая";
    case 2: return "Карма: плохая";
    case 4: return "Карма: хорошая";
    case 5: return "Карма: добрая";
    default: return "Карма: нейтральная";
    }
}

std::string FUiRuntime::CharacterProgressText(const FUiControlDef& control) const
{
    const auto& slot = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))];

    if (!slot.Present) { return {}; }

    auto pairText = [](int32 current, int32 maximum)
    {
        return std::to_string(std::max(0, current)) + " / " + std::to_string(std::max(0, maximum));
    };

    if (control.Id == 41) { return pairText(slot.TitleXp, slot.TitleNextXp); }

    if (control.Id == 42) { return pairText(slot.DegreeXp, slot.DegreeNextXp); }

    if (control.Id == 45) { return pairText(slot.CurrentHp, slot.MaxHp); }

    if (control.Id == 46) { return pairText(slot.CurrentMp, slot.MaxMp); }

    if (control.Id == 47) { return pairText(slot.CurrentSatiety, slot.MaxSatiety); }

    return {};
}

std::string FUiRuntime::ModalControlText(const FUiControlDef& control) const
{
    if (Modal == EUiModalDialog::CharacterDelete)
    {
        if (control.Id == 3 || EqualsNoCase(control.ClassId, "HYPER_TEXT") || EqualsNoCase(control.ClassId, "TEXTLIST") || EqualsNoCase(control.ClassId, "TEXT")) { return ModalText; }

        if (control.Id == 4 || EqualsNoCase(control.ClassId, "EDIT")) { return Actions.FocusedControlId == control.Id ? ModalEditText + "_" : ModalEditText; }
    }

    if ((Modal == EUiModalDialog::CharacterCreate || Modal == EUiModalDialog::CharacterExit) && (EqualsNoCase(control.ClassId, "TEXTLIST") || EqualsNoCase(control.ClassId, "TEXT") || EqualsNoCase(control.ClassId, "HYPER_TEXT"))) { return ModalText; }

    return control.TextKey.empty() ? std::string{} : ResolveText(control.TextKey);
}

bool FUiRuntime::IsModalActionAllowed(const FUiControlDef& control) const
{
    if (Modal == EUiModalDialog::CharacterDelete && control.Id == 5) { return ModalEditMatchesSelectedCharacter(); }

    return !control.Disabled;
}

std::string FUiRuntime::CharacterControlText(const FUiControlDef& control) const
{
    if (control.Id >= 63 && control.Id <= 65)
    {
        const int32 slot = control.Id - 63;
        const auto& info = CharacterSlotState[static_cast<size_t>(slot)];

        if (slot == std::clamp(SelectedSlot, 0, 2) && !info.Present && info.CanCreate) { return {}; }

        return info.Present ? WideToUtf8(info.Name) : EmptyCharacterSlotText();
    }

    if (control.Id >= 60 && control.Id <= 62)
    {
        const int32 slot = control.Id - 60;
        std::wstring text = CharacterNameEdits[static_cast<size_t>(slot)];

        if (ActiveCharacterEditId == control.Id && !CharacterActionLocked)
        {
            text.push_back(L'_');
        }

        return WideToUtf8(text);
    }

    if (control.Id == 2) { const auto& info = CharacterSlotState[static_cast<size_t>(std::clamp(SelectedSlot, 0, 2))]; return CharacterGenderText(info.Present ? info.Female : Appearance.Gender != 0); }

    if (control.Id == 43) { return CharacterTitleText(); }

    if (control.Id == 44) { return CharacterDegreeText(); }

    if ((control.Id >= 17 && control.Id <= 20) || (control.Id >= 29 && control.Id <= 32) || (control.Id >= 48 && control.Id <= 53)) { return std::to_string(CharacterTextValue(control.Id)); }

    if (control.Id == 55) { return {}; }

    if (control.Id == 56) { return CharacterKarmaText(); }

    return control.TextKey.empty() ? std::string{} : ResolveText(control.TextKey);
}
