#include "UI/UiDocumentParser.h"
#include "UI/UiRuntimeParser.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"

namespace
{
    struct FUiLineCommand
    {
        explicit FUiLineCommand(std::string_view line) : Input(std::string(line))
        {
            Input >> Key;
            Key = Common::ToLower(std::move(Key));
        }

        std::string Key;
        std::istringstream Input;
    };

    int32 ParseControlIdFromComment(const std::string& line)
    {
        const size_t marker = Common::ToLower(line).find("id");
        if (marker == std::string::npos) { return 0; }
        const size_t eq = line.find('=', marker);
        if (eq == std::string::npos) { return 0; }
        std::istringstream input(line.substr(eq + 1));
        int32 id = 0;
        input >> id;
        return id;
    }

    bool ParseBool(std::istringstream& input, bool defaultValue = false)
    {
        std::string value;
        input >> value;
        value = Common::ToLower(std::move(value));
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

    std::string ParseQuoted(std::string_view line);
    std::string TailAfterQuoted(std::string_view line);

    EUiPopupEffect ParsePopupEffectName(std::string value)
    {
        value = Common::ToLower(std::move(value));
        if (value == "alpha_in") { return EUiPopupEffect::AlphaIn; }
        if (value == "alpha_out") { return EUiPopupEffect::AlphaOut; }
        if (value == "move_left") { return EUiPopupEffect::MoveLeft; }
        if (value == "move_right") { return EUiPopupEffect::MoveRight; }
        if (value == "move_top") { return EUiPopupEffect::MoveTop; }
        if (value == "move_bottom") { return EUiPopupEffect::MoveBottom; }
        return EUiPopupEffect::None;
    }

    FUiPopupAnimationDesc ParsePopupAnimation(std::string_view line)
    {
        FUiLineCommand command(line);
        std::string effectName;
        command.Input >> effectName;
        const bool quoted = !effectName.empty() && effectName.front() == '"';
        if (quoted) { effectName = ParseQuoted(line); }
        FUiPopupAnimationDesc effect;
        effect.Effect = ParsePopupEffectName(effectName);
        if (effect.Effect == EUiPopupEffect::None) { return effect; }
        if (quoted)
        {
            std::istringstream tail(TailAfterQuoted(line));
            tail >> effect.Duration >> effect.OffsetX >> effect.OffsetY;
        }
        else
        {
            command.Input >> effect.Duration >> effect.OffsetX >> effect.OffsetY;
        }
        if (effect.Duration <= 0.0f) { effect.Duration = 0.20f; }
        return effect;
    }

    std::vector<std::string> LinesOf(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::string ParseQuoted(std::string_view line)
    {
        const size_t begin = line.find('"');
        if (begin == std::string_view::npos) { return {}; }
        const size_t end = line.find('"', begin + 1);
        if (end == std::string_view::npos) { return {}; }
        return std::string(line.substr(begin + 1, end - begin - 1));
    }

    std::string TailAfterQuoted(std::string_view line)
    {
        const size_t begin = line.find('"');
        if (begin == std::string_view::npos) { return {}; }
        const size_t end = line.find('"', begin + 1);
        if (end == std::string_view::npos || end + 1 >= line.size()) { return {}; }
        return std::string(line.substr(end + 1));
    }

    std::string ReadTokenOrQuoted(FUiLineCommand& command, std::string_view line)
    {
        std::string value;
        command.Input >> value;
        return value.empty() ? ParseQuoted(line) : value;
    }

    bool StartsWithToken(std::string_view line, std::string_view token)
    {
        FUiLineCommand command(line);
        return Common::EqualsNoCase(command.Key, token);
    }

    bool IsOpenBrace(std::string_view line) { return line == "{"; }
    bool IsCloseBrace(std::string_view line) { return line == "}"; }

    TResult<std::string> LoadUiText(const FResourceManager& resources, std::string_view logicalName)
    {
        auto blob = resources.Load(logicalName);
        if (!blob.IsOk()) { return blob.Status(); }
        return Common::WideToUtf8(Common::Cp1251BytesToWide(blob.Value().Bytes));
    }

    void ApplySubButtonLine(FUiSubButtonDef& button, std::string_view line)
    {
        FUiLineCommand command(line);
        if (command.Key == "position") { command.Input >> button.X >> button.Y; }
        else if (command.Key == "size") { command.Input >> button.W >> button.H; }
        else if (command.Key == "checkedimage") { button.CheckedImage = ParseQuoted(line); }
        else if (command.Key == "focusedimage") { button.FocusedImage = ParseQuoted(line); }
        else if (command.Key == "disabledimage") { button.DisabledImage = ParseQuoted(line); }
        else if (command.Key == "uncheckedimage") { button.UncheckedImage = ParseQuoted(line); }
    }

    void ApplySpriteLine(FUiSpriteDef& sprite, std::string_view line)
    {
        FUiLineCommand command(line);
        if (command.Key == "name") { sprite.Name = ParseQuoted(line); }
        else if (command.Key == "size") { command.Input >> sprite.Width >> sprite.Height; }
        else if (command.Key == "texture")
        {
            FUiSpritePiece piece;
            piece.TextureName = ParseQuoted(line);
            std::istringstream coords(TailAfterQuoted(line));
            if (coords >> piece.SrcLeft >> piece.SrcTop >> piece.SrcRight >> piece.SrcBottom >> piece.DstLeft >> piece.DstTop >> piece.DstRight >> piece.DstBottom) { sprite.Pieces.push_back(std::move(piece)); }
        }
        else if (command.Key == "tcoords")
        {
            int32 index = -1;
            command.Input >> index;
            if (index < 0 || index >= static_cast<int32>(sprite.Pieces.size())) { return; }
            auto& piece = sprite.Pieces[static_cast<size_t>(index)];
            for (FUiTexCoord& coord : piece.TexCoords) { command.Input >> coord.U >> coord.V; }
            piece.HasTexCoords = true;
        }
    }

    void ApplyControlLine(FUiControlDef& control, std::string_view line)
    {
        FUiLineCommand command(line);
        if (command.Key == "classid") { command.Input >> control.ClassId; }
        else if (command.Key == "position") { command.Input >> control.Rect.X >> control.Rect.Y; }
        else if (command.Key == "size") { command.Input >> control.Rect.W >> control.Rect.H; }
        else if (command.Key == "font") { command.Input >> control.Font; }
        else if (command.Key == "textcolor") { control.TextColor = ParseColor(command.Input, control.TextColor); }
        else if (command.Key == "disabledcolor") { control.DisabledColor = ParseColor(command.Input, control.DisabledColor); }
        else if (command.Key == "focuscolor") { control.FocusColor = ParseColor(command.Input, control.FocusColor); }
        else if (command.Key == "windowtext") { control.TextKey = ReadTokenOrQuoted(command, line); }
        else if (command.Key == "textformat") { control.TextCenter = line.find("CENTER") != std::string_view::npos; }
        else if (command.Key == "maxsymbols") { command.Input >> control.MaxSymbols; }
        else if (command.Key == "enteredonfocus") { control.EnteredOnFocus = ParseBool(command.Input, control.EnteredOnFocus); }
        else if (command.Key == "hotkey") { control.HotKey = ParseQuoted(line); }
        else if (command.Key == "hypertext") { control.HyperTextResource = ParseQuoted(line); }
        else if (command.Key == "imageoffset") { command.Input >> control.ImageOffset.X >> control.ImageOffset.Y; }
        else if (command.Key == "group") { command.Input >> control.Group; }
        else if (command.Key == "checkedimage") { control.CheckedImage = ParseQuoted(line); }
        else if (command.Key == "uncheckedimage") { control.UncheckedImage = ParseQuoted(line); }
        else if (command.Key == "focusedimage") { control.FocusedImage = ParseQuoted(line); }
        else if (command.Key == "image") { control.ImageName = ParseQuoted(line); }
        else if (command.Key == "drawmethod") { control.DrawSpriteName = ParseQuoted(line); }
        else if (command.Key == "windowhelp") { control.WindowHelp = ParseQuoted(line); }
        else if (command.Key == "slotempty") { control.SlotEmptyImage = ParseQuoted(line); }
        else if (command.Key == "slotfull") { control.SlotFullImage = ParseQuoted(line); }
        else if (command.Key == "slotborder") { control.SlotBorderImage = ParseQuoted(line); }
        else if (command.Key == "scrollspr") { control.ScrollSpriteName = ParseQuoted(line); std::istringstream sizeInput(TailAfterQuoted(line)); sizeInput >> control.ScrollSpriteWidth >> control.ScrollSpriteHeight; }
        else if (command.Key == "deltastep") { command.Input >> control.DeltaStep; }
        else if (command.Key == "range") { command.Input >> control.RangeMin >> control.RangeMax; }
        else if (command.Key == "progresspos") { command.Input >> control.ProgressPos; }
        else if (command.Key == "statusshow") { command.Input >> control.StatusShow; }
        else if (command.Key == "statuspos") { command.Input >> control.StatusPos.X >> control.StatusPos.Y; }
        else if (command.Key == "password") { control.Password = ParseBool(command.Input, control.Password); }
        else if (command.Key == "buttonstyle") { control.SendQuit = line.find("SEND_QUIT") != std::string_view::npos; control.SendHelp = line.find("SEND_HELP") != std::string_view::npos; }
        else if (command.Key == "hidden") { control.Hidden = ParseBool(command.Input, control.Hidden); }
        else if (command.Key == "disabled") { control.Disabled = ParseBool(command.Input, control.Disabled); }
    }

    void ApplyWindowLine(FUiWindowDef& window, std::string_view line)
    {
        FUiLineCommand command(line);
        if (command.Key == "windowname") { window.Name = ParseQuoted(line); }
        else if (window.Name.empty()) { return; }
        else if (command.Key == "windowtext") { window.TextKey = ReadTokenOrQuoted(command, line); }
        else if (command.Key == "position") { command.Input >> window.Rect.X >> window.Rect.Y; }
        else if (command.Key == "size") { command.Input >> window.Rect.W >> window.Rect.H; }
        else if (command.Key == "font") { command.Input >> window.Font; }
        else if (command.Key == "textcolor") { window.TextColor = ParseColor(command.Input, window.TextColor); }
        else if (command.Key == "recttitle") { command.Input >> window.TitleRect.X >> window.TitleRect.Y >> window.TitleRect.W >> window.TitleRect.H; }
        else if (command.Key == "alignwin") { window.AlignCenterX = line.find("CENTER_X") != std::string_view::npos; window.AlignCenterY = line.find("CENTER_Y") != std::string_view::npos; window.AlignRightX = line.find("RIGHT_X") != std::string_view::npos; window.AlignRightY = line.find("RIGHT_Y") != std::string_view::npos; }
        else if (command.Key == "savelastposition") { window.SaveLastPosition = ParseBool(command.Input, window.SaveLastPosition); }
        else if (command.Key == "candragdrop") { window.CanDragDrop = ParseBool(command.Input, window.CanDragDrop); }
        else if (command.Key == "cannotcross") { window.CanNotCross = ParseBool(command.Input, window.CanNotCross); }
        else if (command.Key == "cangotop") { window.CanGoTop = ParseBool(command.Input, window.CanGoTop); }
        else if (command.Key == "escapehandle") { window.EscapeHandle = ParseBool(command.Input, window.EscapeHandle); }
        else if (command.Key == "showeffect") { window.ShowEffect = ParsePopupAnimation(line); }
        else if (command.Key == "hideeffect") { window.HideEffect = ParsePopupAnimation(line); }
        else if (command.Key == "drawmethod") { window.DrawNone = line.find("NONE") != std::string_view::npos; window.DrawSpriteName = ParseQuoted(line); }
    }
}

TResult<FUiStringTable> FUiDocumentParser::LoadStringTableFromResource(const FResourceManager& resources, std::string_view logicalName) const
{
    auto text = LoadUiText(resources, logicalName);
    if (!text.IsOk()) { return text.Status(); }

    FUiStringTable strings;
    for (std::string line : LinesOf(text.Value()))
    {
        line = Common::Trim(Common::StripCppComment(std::move(line)));
        if (!StartsWithToken(line, "string")) { continue; }
        FUiLineCommand command(line);
        std::string key;
        command.Input >> key;
        if (!key.empty()) { strings[std::move(key)] = ParseQuoted(line); }
    }

    return strings;
}

TResult<FUiWindowDef> FUiDocumentParser::LoadWindowFromResource(const FResourceManager& resources, std::string_view logicalName) const
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

    for (const std::string& rawLine : LinesOf(text.Value()))
    {
        std::string line = Common::Trim(Common::StripCppComment(rawLine));
        if (line.empty()) { continue; }
        if (!inControl && !inSprite && !window.Name.empty() && IsCloseBrace(line)) { break; }
        if (!inControl && !inSprite && StartsWithToken(line, "sprite")) { pendingSprite = true; continue; }
        if (!inControl && !inSprite && Common::ToLower(rawLine).find("control") != std::string::npos) { pendingControl = true; pendingId = ParseControlIdFromComment(rawLine); }

        if (pendingSprite && IsOpenBrace(line)) { pendingSprite = false; inSprite = true; sprite = FUiSpriteDef{}; continue; }
        if (inSprite)
        {
            if (IsCloseBrace(line))
            {
                if (!sprite.Name.empty()) { window.Sprites[Common::ToLower(sprite.Name)] = std::move(sprite); }
                inSprite = false;
                continue;
            }
            ApplySpriteLine(sprite, line);
            continue;
        }

        if (pendingControl && IsOpenBrace(line)) { inControl = true; pendingControl = false; controlNestedDepth = 0; control = FUiControlDef{}; control.Id = pendingId; continue; }
        if (inControl)
        {
            const std::string lowerLine = Common::ToLower(line);
            if (controlNestedDepth == 0 && (lowerLine == "leftbutton" || lowerLine == "rightbutton")) { pendingControlSection = lowerLine; continue; }
            if (IsOpenBrace(line))
            {
                ++controlNestedDepth;
                if (controlNestedDepth == 1 && !pendingControlSection.empty()) { activeControlSection = std::move(pendingControlSection); pendingControlSection.clear(); }
                continue;
            }
            if (IsCloseBrace(line))
            {
                if (controlNestedDepth > 0)
                {
                    --controlNestedDepth;
                    if (controlNestedDepth == 0) { activeControlSection.clear(); }
                    continue;
                }
                if (control.Id == 0) { control.Id = -1 - static_cast<int32>(window.Controls.size()); }
                window.Controls.push_back(std::move(control));
                inControl = false;
                continue;
            }
            if (controlNestedDepth > 0)
            {
                if (controlNestedDepth == 1 && !activeControlSection.empty()) { ApplySubButtonLine(activeControlSection == "leftbutton" ? control.LeftButton : control.RightButton, line); }
                continue;
            }
            ApplyControlLine(control, line);
            continue;
        }

        ApplyWindowLine(window, line);
    }

    if (window.Name.empty()) { return FStatus::Error(EStatusCode::InvalidData, "UI window has no windowName: " + std::string(logicalName)); }
    return window;
}


TResult<FUiStringTable> LoadUiStringTableFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    return FUiDocumentParser{}.LoadStringTableFromResource(resources, logicalName);
}

TResult<FUiWindowDef> LoadUiWindowFromResource(const FResourceManager& resources, std::string_view logicalName)
{
    return FUiDocumentParser{}.LoadWindowFromResource(resources, logicalName);
}
