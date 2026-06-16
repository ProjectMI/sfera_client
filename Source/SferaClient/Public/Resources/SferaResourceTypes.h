#pragma once
#include "SferaBase.h"

enum class ESferaResourceKind
{
    Unknown,
    Config,
    ScriptBytecode,
    ScriptLibrary,
    UserInterface,
    HyperText,
    Texture,
    CursorImage,
    FontDefinition,
    SoundDefinition,
    SoundWave,
    Model,
    Skeleton,
    Material,
    Landscape,
    Shader,
    FileList,
    ExecutableIgnored
};

std::string_view SferaResourceKindToString(ESferaResourceKind Kind);

struct SferaResourceRecord
{
    std::string LogicalPath;
    std::string DiskPath;
    std::string ExtensionLower;
    ESferaResourceKind Kind = ESferaResourceKind::Unknown;
    SferaUInt64 SizeBytes = 0;
    bool bExists = false;
    bool bBootstrapCritical = false;
};

struct SferaResourceStats
{
    int TotalFiles = 0;
    int ConfigFiles = 0;
    int ScriptFiles = 0;
    int UserInterfaceFiles = 0;
    int TextureFiles = 0;
    int SoundFiles = 0;
    int ModelFiles = 0;
    int LandscapeFiles = 0;
    int ShaderFiles = 0;
    int MissingBootstrapFiles = 0;
};
