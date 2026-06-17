#include "SferaUiControlCatalog.h"
#include "SferaResourceManager.h"
#include <algorithm>
#include <cctype>
#include <sstream>

void SferaUiControlCatalog::Build(const SferaResourceManager& Resources)
{
    Clear();
    AddFamily("ButtonCtrl", "Interface\\Button.cpp", { "button", "checkedImage", "focuscolor" }, 6);
    AddFamily("CheckBox", "Interface\\CheckBox.cpp", { "checkbox", "checkedImage", "imageOffset", "textOffset" }, 5);
    AddFamily("EditCtrl", "Interface\\EditCtrl.cpp", { "edit", "text", "focuscolor" }, 4);
    AddFamily("FilterListCtrl", "Interface\\FilterListCtrl.cpp", { "filter", "list", "text" }, 3);
    AddFamily("FontPicker", "Interface\\FontPicker.cpp", { "font", "fontPicker", "text" }, 5);
    AddFamily("HyperTextCtrl", "Interface\\HyperTextCtrl.cpp", { "hypertext", "Sounds\\in_link.wav", "textFormat" }, 11);
    AddFamily("HyperTextChatListControl", "Interface\\HyperTextChatListControl.cpp", { "chat", "hyperText", "textFormat" }, 8);
    AddFamily("HyperTextEditControl", "Interface\\HyperTextEditControl.cpp", { "hyperTextEdit", "text", "selection" }, 7);
    AddFamily("ImageCtrl", "Interface\\ImageCtrl.cpp", { "image", "sprite", "alpha" }, 4);
    AddFamily("InterfaceWindow", "Interface\\Interface.cpp", { "windowUI", "loadscreen", "control.cfg" }, 14);
    AddFamily("ListCtrl", "Interface\\ListCtrl.cpp", { "list", "item", "scroll" }, 6);
    AddFamily("MenuListControl", "Interface\\MenuListControl.cpp", { "menu", "item", "SEND_QUIT" }, 3);
    AddFamily("MiniHelpCtrl", "Interface\\MiniHelpCtrl.cpp", { "minihelp", "help", "Language\\helpindex.hts" }, 3);
    AddFamily("MinimapControl", "Interface\\MinimapControl.cpp", { "minimap", "map", "player" }, 3);
    AddFamily("ProgressBar", "Interface\\ProgressBar.cpp", { "progress", "image", "value" }, 3);
    AddFamily("RadioButtonCtrl", "Interface\\RadioButton.cpp", { "radio", "checkedImage", "group" }, 3);
    AddFamily("RichEditCtrl", "Interface\\RichEditCtrl.cpp", { "richEdit", "text", "selection" }, 3);
    AddFamily("ScrollBar", "Interface\\ScrollBar.cpp", { "scrollbar", "thumb", "button" }, 4);
    AddFamily("SliderCtrl", "Interface\\SliderCtrl.cpp", { "slider", "thumb", "range" }, 4);
    AddFamily("SlotCtrl", "Interface\\SlotCtrl.cpp", { "slot", "item", "drag" }, 5);
    AddFamily("SpinButton", "Interface\\SpinButton.cpp", { "spin", "up", "down" }, 4);
    AddFamily("TextCtrl", "Interface\\TextCtrl.cpp", { "text", "color", "font" }, 3);
    AddFamily("ToolTipCtrl", "Interface\\ToolTipCtrl.cpp", { "tooltip", "text", "delay" }, 3);
    AddFamily("WebBrowserControl", "Interface\\WebBrowserControl.cpp", { "browser", "url", "html" }, 3);
    AddFamily("Cursor", "Interface\\Cursor.cpp", { "cursor", "cursor1", "EN_CROSS" }, 4);
    BindResources(Resources);
}

void SferaUiControlCatalog::Clear()
{
    Families.clear();
    StartupBindings.clear();
}

std::string SferaUiControlCatalog::BuildSummaryText() const
{
    int FunctionTotal = 0;
    for (const SferaUiControlFamily& Family : Families)
    {
        FunctionTotal += Family.FunctionCount;
    }

    std::ostringstream Stream;
    Stream << "UI catalog: " << Families.size() << " families, " << FunctionTotal << " recovered functions, " << StartupBindings.size() << " resource bindings";
    return Stream.str();
}

void SferaUiControlCatalog::AddFamily(const char* Name, const char* SourceFile, std::initializer_list<const char*> Keys, int FunctionCount)
{
    SferaUiControlFamily Family;
    Family.Name = Name;
    Family.SourceFile = SourceFile;
    Family.FunctionCount = FunctionCount;
    for (const char* Key : Keys)
    {
        Family.ResourceKeys.push_back(Key);
    }
    Families.push_back(Family);
}

void SferaUiControlCatalog::BindResources(const SferaResourceManager& Resources)
{
    std::vector<const SferaResourceRecord*> UiFiles = Resources.FindByKind(ESferaResourceKind::UserInterface);
    for (const SferaResourceRecord* Record : UiFiles)
    {
        if (Record && Record->bExists)
        {
            AddBinding(*Record, ClassifyUiResource(*Record));
        }
    }

    std::vector<const SferaResourceRecord*> FontFiles = Resources.FindByKind(ESferaResourceKind::FontDefinition);
    for (const SferaResourceRecord* Record : FontFiles)
    {
        if (Record && Record->bExists)
        {
            AddBinding(*Record, "font factory input");
        }
    }

    std::vector<const SferaResourceRecord*> LanguageFiles = Resources.FindByKind(ESferaResourceKind::HyperText);
    for (const SferaResourceRecord* Record : LanguageFiles)
    {
        if (Record && Record->bExists)
        {
            AddBinding(*Record, "hypertext and help text");
        }
    }
}

void SferaUiControlCatalog::AddBinding(const SferaResourceRecord& Record, const std::string& Purpose)
{
    SferaUiStartupBinding Binding;
    Binding.LogicalPath = Record.LogicalPath;
    Binding.Kind = Record.Kind;
    Binding.Purpose = Purpose;
    StartupBindings.push_back(Binding);
}

std::string SferaUiControlCatalog::ClassifyUiResource(const SferaResourceRecord& Record) const
{
    std::string Path = Record.LogicalPath;
    std::transform(Path.begin(), Path.end(), Path.begin(), [](unsigned char Value) { return static_cast<char>(std::tolower(Value)); });
    if (Path.find("load") != std::string::npos)
    {
        return "load screen window";
    }
    if (Path.find("effect") != std::string::npos)
    {
        return "effect window";
    }
    if (Path.find("chat") != std::string::npos)
    {
        return "chat or hypertext window";
    }
    if (Path.find("control") != std::string::npos)
    {
        return "control binding config";
    }
    return "window/control layout";
}
