#include "ResourceLoader/ResourceTypes.h"
#include <algorithm>
#include <cctype>

namespace Sfera {
const char* ToString(EResourceKind kind) {
    switch (kind) { case EResourceKind::Config: return "Config"; case EResourceKind::Mbc: return "MBC"; case EResourceKind::Ui: return "UI"; case EResourceKind::Texture: return "Texture"; case EResourceKind::Model: return "Model"; case EResourceKind::Material: return "Material"; case EResourceKind::Landscape: return "Landscape"; case EResourceKind::Script: return "Script"; case EResourceKind::Sound: return "Sound"; case EResourceKind::Binary: return "Binary"; default: return "Unknown"; }
}

EResourceKind GuessResourceKind(const FPath& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".cfg") { return EResourceKind::Config; }
    if (ext == ".mbc") { return EResourceKind::Mbc; }
    if (ext == ".ui") { return EResourceKind::Ui; }
    if (ext == ".dds" || ext == ".tga" || ext == ".bmp" || ext == ".png") { return EResourceKind::Texture; }
    if (ext == ".mdl" || ext == ".ssm" || ext == ".msh") { return EResourceKind::Model; }
    if (ext == ".mtr" || ext == ".mtx") { return EResourceKind::Material; }
    std::string generic = path.generic_string();
    std::transform(generic.begin(), generic.end(), generic.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".siz" || ext == ".map" || ext == ".lnd" || (ext == ".bin" && (generic.find("landscape") != std::string::npos || generic.find("xadd/snowpath") != std::string::npos)) || (ext == ".txt" && generic.find("landscape") != std::string::npos)) { return EResourceKind::Landscape; }
    if (ext == ".txt" || ext == ".scr") { return EResourceKind::Script; }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") { return EResourceKind::Sound; }
    return EResourceKind::Binary;
}
}
