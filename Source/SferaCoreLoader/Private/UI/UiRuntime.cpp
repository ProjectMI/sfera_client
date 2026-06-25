#include "UI/UiRuntime.h"
#include "Common/SferaGameConstants.h"
#include "Common/StringUtils.h"
#include "Common/TextEncoding.h"

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

    GameWindowDefs.clear();
    GameWindowVisible.clear();
    const std::vector<std::string> gameWindowResources = {
        "effects/system_left.ui",
        "effects/system_leftmin.ui",
        "effects/system_right.ui",
        "effects/system_rightmin.ui",
        "effects/chat.ui",
        "effects/chat_st2.ui",
        "effects/chat_sys.ui",
        "effects/inventory.ui",
        "effects/statinfo.ui",
        "effects/puppet.ui",
        "effects/quickitems.ui",
        "effects/hotkeys.ui",
        "effects/mantrabook.ui",
        "effects/journal.ui",
        "effects/clan.ui",
        "effects/group.ui",
        "effects/minimap.ui",
        "effects/bigmap.ui",
        "effects/help.ui",
        "effects/options.ui",
        "effects/gfxoptions.ui",
        "effects/soundopt.ui",
        "effects/controls.ui",
        "effects/intoptions.ui",
        "effects/authors.ui"
    };
    const std::vector<std::string> initiallyVisible = {"system_left", "system_right", "chat_st2", "chat_sys"};
    for (const auto& resourceName : gameWindowResources)
    {
        auto gameWindow = LoadUiWindowFromResource(resources, resourceName);
        if (!gameWindow.IsOk())
        {
            if (logger) { logger->Warning("game UI window is not available: " + resourceName + "; " + gameWindow.Status().Message()); }
            continue;
        }
        bool visible = false;
        for (const auto& name : initiallyVisible)
        {
            if (Common::EqualsNoCase(gameWindow.Value().Name, name))
            {
                visible = true;
                break;
            }
        }
        GameWindowVisible.push_back(visible);
        GameWindowDefs.push_back(std::move(gameWindow.Value()));
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
        logger->Info("UI runtime initialized: strings=" + std::to_string(StringTable.size()) + ", connection=" + Connection.Name + ", controls=" + std::to_string(Connection.Controls.size()) + ", pick_person=" + PickPerson.Name + ", create_person=" + CreatePerson.Name + ", connect_message=" + ConnectMessage.Name + ", message=" + Message.Name + ", game_windows=" + std::to_string(GameWindowDefs.size()));
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

    if (Status.size() > SferaUi::MaxStatusLines)
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
    const std::string name = Common::WideToUtf8(SelectedCharacterName());
    ModalText = Bootstrap.Lang == 1 ? "Type character name to delete: " + name : "Введите имя персонажа для удаления: " + name;
    ModalEditText.clear();
    Actions.FocusedControlId = SferaUi::DeleteConfirmEditId;
    Actions.LastAction = "character_delete_dialog";
}

void FUiRuntime::DismissModal()
{
    Modal = EUiModalDialog::None;
    ModalText.clear();
    ModalEditText.clear();
    Actions.PressedControlId = 0;
}

