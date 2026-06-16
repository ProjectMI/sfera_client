#include "SferaResourceTypes.h"

const char* SferaResourceKindToString(ESferaResourceKind Kind)
{
    switch (Kind)
    {
    case ESferaResourceKind::Config: return "Config";
    case ESferaResourceKind::ScriptBytecode: return "ScriptBytecode";
    case ESferaResourceKind::ScriptLibrary: return "ScriptLibrary";
    case ESferaResourceKind::UserInterface: return "UserInterface";
    case ESferaResourceKind::HyperText: return "HyperText";
    case ESferaResourceKind::Texture: return "Texture";
    case ESferaResourceKind::CursorImage: return "CursorImage";
    case ESferaResourceKind::FontDefinition: return "FontDefinition";
    case ESferaResourceKind::SoundDefinition: return "SoundDefinition";
    case ESferaResourceKind::SoundWave: return "SoundWave";
    case ESferaResourceKind::Model: return "Model";
    case ESferaResourceKind::Skeleton: return "Skeleton";
    case ESferaResourceKind::Material: return "Material";
    case ESferaResourceKind::Landscape: return "Landscape";
    case ESferaResourceKind::Shader: return "Shader";
    case ESferaResourceKind::FileList: return "FileList";
    case ESferaResourceKind::ExecutableIgnored: return "ExecutableIgnored";
    default: return "Unknown";
    }
}
