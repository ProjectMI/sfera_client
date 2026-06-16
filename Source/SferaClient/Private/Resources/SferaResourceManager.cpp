#include "SferaResourceManager.h"
#include "SferaStringUtil.h"
#include <filesystem>
#include <fstream>
#include <sstream>

bool SferaResourceManager::Initialize(const char* RootDirectory)
{
    Root = RootDirectory && RootDirectory[0] ? RootDirectory : ".";
    ResetCatalog();
    return true;
}

void SferaResourceManager::Shutdown()
{
    ResetCatalog();
    Root.clear();
}

void SferaResourceManager::ResetCatalog()
{
    Resources.clear();
    ResourceIndexByLowerPath.clear();
    Warnings.clear();
    Stats = SferaResourceStats();
}

bool SferaResourceManager::ScanBootstrapResources()
{
    ResetCatalog();

    // Paths come from the current IDA string/source-unit slice. Missing records are kept deliberately:
    // they are useful during bring-up because they show which original bootstrap resource is still absent.
    RegisterExpectedResource("config.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("connect.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("connectn.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("control.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("debug.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("fonts.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("Landscape\\zoning.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("Landscape\\zoningHaron.cfg", ESferaResourceKind::Config, false);
    RegisterExpectedResource("Models\\Materials.cfg", ESferaResourceKind::Material, false);
    RegisterExpectedResource("matbase.dat", ESferaResourceKind::Material, false);
    RegisterExpectedResource("filelist.dat", ESferaResourceKind::FileList, false);
    RegisterExpectedResource("xadd\\subobjs.dat", ESferaResourceKind::Model, false);
    RegisterExpectedResource("fonts\\font.txt", ESferaResourceKind::FontDefinition, false);
    RegisterExpectedResource("Effects\\loadscreen.ui", ESferaResourceKind::UserInterface, false);
    RegisterExpectedResource("Effects\\sprites.ui", ESferaResourceKind::UserInterface, false);
    RegisterExpectedResource("Language\\strings.ui", ESferaResourceKind::UserInterface, false);
    RegisterExpectedResource("Language\\helpindex.hts", ESferaResourceKind::HyperText, false);
    RegisterExpectedResource("WorldMap.bmp", ESferaResourceKind::Texture, false);
    RegisterExpectedResource("__debug.mbc", ESferaResourceKind::ScriptBytecode, false);

    ScanDirectory(".", true);
    RebuildStats();
    return true;
}

bool SferaResourceManager::ScanDirectory(const std::string& RelativeDirectory, bool bRecursive)
{
    const std::string Normalized = SferaStringUtil::NormalizeLogicalPath(RelativeDirectory.empty() ? "." : RelativeDirectory);
    const std::filesystem::path ScanRoot = std::filesystem::path(Root) / Normalized;

    std::error_code Ec;
    if (!std::filesystem::exists(ScanRoot, Ec))
    {
        return false;
    }

    try
    {
        if (bRecursive)
        {
            for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(ScanRoot))
            {
                if (Entry.is_regular_file())
                {
                    const std::filesystem::path Rel = std::filesystem::relative(Entry.path(), std::filesystem::path(Root), Ec);
                    AddDiskFile(Entry.path().string(), Ec ? Entry.path().filename().string() : Rel.string());
                }
            }
        }
        else
        {
            for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(ScanRoot))
            {
                if (Entry.is_regular_file())
                {
                    const std::filesystem::path Rel = std::filesystem::relative(Entry.path(), std::filesystem::path(Root), Ec);
                    AddDiskFile(Entry.path().string(), Ec ? Entry.path().filename().string() : Rel.string());
                }
            }
        }
    }
    catch (const std::exception& Ex)
    {
        Warnings.push_back(std::string("ScanDirectory failed: ") + Ex.what());
        return false;
    }

    RebuildStats();
    return true;
}

bool SferaResourceManager::RegisterExpectedResource(const std::string& LogicalPath, ESferaResourceKind Kind, bool bCritical)
{
    SferaResourceRecord Record;
    Record.LogicalPath = SferaStringUtil::NormalizeLogicalPath(LogicalPath);
    Record.DiskPath = MakeDiskPath(Record.LogicalPath);
    Record.ExtensionLower = SferaStringUtil::GetExtensionLower(Record.LogicalPath);
    Record.Kind = Kind == ESferaResourceKind::Unknown ? ClassifyLogicalPath(Record.LogicalPath) : Kind;
    Record.bBootstrapCritical = bCritical;

    std::error_code Ec;
    const std::filesystem::path Disk(Record.DiskPath);
    Record.bExists = std::filesystem::exists(Disk, Ec) && std::filesystem::is_regular_file(Disk, Ec);
    if (Record.bExists)
    {
        Record.SizeBytes = static_cast<SferaUInt64>(std::filesystem::file_size(Disk, Ec));
    }

    AddOrUpdateRecord(Record);
    return Record.bExists || !bCritical;
}

void SferaResourceManager::AddDiskFile(const std::string& DiskPath, const std::string& LogicalPath)
{
    SferaResourceRecord Record;
    Record.LogicalPath = SferaStringUtil::NormalizeLogicalPath(LogicalPath);
    Record.DiskPath = DiskPath;
    Record.ExtensionLower = SferaStringUtil::GetExtensionLower(Record.LogicalPath);
    Record.Kind = ClassifyLogicalPath(Record.LogicalPath);
    Record.bExists = true;

    std::error_code Ec;
    Record.SizeBytes = static_cast<SferaUInt64>(std::filesystem::file_size(std::filesystem::path(DiskPath), Ec));
    if (Ec)
    {
        Record.SizeBytes = 0;
    }

    AddOrUpdateRecord(Record);
}

void SferaResourceManager::AddOrUpdateRecord(const SferaResourceRecord& Record)
{
    const std::string Key = SferaStringUtil::ToLower(Record.LogicalPath);
    auto Existing = ResourceIndexByLowerPath.find(Key);
    if (Existing != ResourceIndexByLowerPath.end())
    {
        SferaResourceRecord& Old = Resources[Existing->second];
        const bool bWasCritical = Old.bBootstrapCritical;
        Old = Record;
        Old.bBootstrapCritical = Old.bBootstrapCritical || bWasCritical;
        return;
    }

    ResourceIndexByLowerPath[Key] = Resources.size();
    Resources.push_back(Record);
}

void SferaResourceManager::RebuildStats()
{
    Stats = SferaResourceStats();
    for (const SferaResourceRecord& Record : Resources)
    {
        if (Record.bExists)
        {
            ++Stats.TotalFiles;
        }
        else if (Record.bBootstrapCritical || !Record.LogicalPath.empty())
        {
            ++Stats.MissingBootstrapFiles;
        }

        switch (Record.Kind)
        {
        case ESferaResourceKind::Config: ++Stats.ConfigFiles; break;
        case ESferaResourceKind::ScriptBytecode:
        case ESferaResourceKind::ScriptLibrary: ++Stats.ScriptFiles; break;
        case ESferaResourceKind::UserInterface:
        case ESferaResourceKind::HyperText:
        case ESferaResourceKind::FontDefinition: ++Stats.UserInterfaceFiles; break;
        case ESferaResourceKind::Texture:
        case ESferaResourceKind::CursorImage: ++Stats.TextureFiles; break;
        case ESferaResourceKind::SoundDefinition:
        case ESferaResourceKind::SoundWave: ++Stats.SoundFiles; break;
        case ESferaResourceKind::Model:
        case ESferaResourceKind::Skeleton:
        case ESferaResourceKind::Material: ++Stats.ModelFiles; break;
        case ESferaResourceKind::Landscape: ++Stats.LandscapeFiles; break;
        case ESferaResourceKind::Shader: ++Stats.ShaderFiles; break;
        default: break;
        }
    }
}

bool SferaResourceManager::ResolvePath(const std::string& LogicalPath, std::string& OutDiskPath) const
{
    const SferaResourceRecord* Record = FindResource(LogicalPath);
    if (Record && Record->bExists)
    {
        OutDiskPath = Record->DiskPath;
        return true;
    }

    const std::string Candidate = MakeDiskPath(LogicalPath);
    std::error_code Ec;
    if (std::filesystem::exists(std::filesystem::path(Candidate), Ec))
    {
        OutDiskPath = Candidate;
        return true;
    }

    return false;
}

const SferaResourceRecord* SferaResourceManager::FindResource(const std::string& LogicalPath) const
{
    const std::string Key = SferaStringUtil::ToLower(SferaStringUtil::NormalizeLogicalPath(LogicalPath));
    auto It = ResourceIndexByLowerPath.find(Key);
    if (It == ResourceIndexByLowerPath.end())
    {
        return nullptr;
    }
    return &Resources[It->second];
}

std::vector<const SferaResourceRecord*> SferaResourceManager::FindByKind(ESferaResourceKind Kind) const
{
    std::vector<const SferaResourceRecord*> Result;
    for (const SferaResourceRecord& Record : Resources)
    {
        if (Record.Kind == Kind && Record.bExists)
        {
            Result.push_back(&Record);
        }
    }
    return Result;
}

std::vector<const SferaResourceRecord*> SferaResourceManager::FindByWildcard(const std::string& Wildcard) const
{
    std::vector<const SferaResourceRecord*> Result;
    for (const SferaResourceRecord& Record : Resources)
    {
        if (Record.bExists && SferaStringUtil::WildcardMatchIgnoreCase(Wildcard, Record.LogicalPath))
        {
            Result.push_back(&Record);
        }
    }
    return Result;
}

bool SferaResourceManager::LoadTextFile(const std::string& LogicalPath, std::string& OutText) const
{
    std::string DiskPath;
    if (!ResolvePath(LogicalPath, DiskPath))
    {
        return false;
    }

    std::ifstream Stream(DiskPath, std::ios::binary);
    if (!Stream)
    {
        return false;
    }

    std::ostringstream Buffer;
    Buffer << Stream.rdbuf();
    OutText = Buffer.str();
    return true;
}

bool SferaResourceManager::ReadBinaryFile(const std::string& LogicalPath, std::vector<SferaUInt8>& OutBytes) const
{
    std::string DiskPath;
    if (!ResolvePath(LogicalPath, DiskPath))
    {
        return false;
    }

    std::ifstream Stream(DiskPath, std::ios::binary);
    if (!Stream)
    {
        return false;
    }

    Stream.seekg(0, std::ios::end);
    const std::streamoff Size = Stream.tellg();
    Stream.seekg(0, std::ios::beg);
    if (Size < 0)
    {
        return false;
    }

    OutBytes.resize(static_cast<size_t>(Size));
    if (!OutBytes.empty())
    {
        Stream.read(reinterpret_cast<char*>(OutBytes.data()), Size);
    }
    return true;
}

ESferaResourceKind SferaResourceManager::ClassifyLogicalPath(const std::string& LogicalPath)
{
    const std::string Path = SferaStringUtil::ToLower(SferaStringUtil::NormalizeLogicalPath(LogicalPath));
    const std::string Ext = SferaStringUtil::GetExtensionLower(Path);

    if (Path.find("textures\\cursors") != std::string::npos && (Ext == ".bmp" || Ext == ".cur" || Ext == ".ani")) return ESferaResourceKind::CursorImage;
    if (Ext == ".cfg") return ESferaResourceKind::Config;
    if (Ext == ".mbc") return ESferaResourceKind::ScriptBytecode;
    if (Ext == ".mbl") return ESferaResourceKind::ScriptLibrary;
    if (Ext == ".ui") return ESferaResourceKind::UserInterface;
    if (Ext == ".hts") return ESferaResourceKind::HyperText;
    if (Ext == ".txt" && Path.find("fonts\\") != std::string::npos) return ESferaResourceKind::FontDefinition;
    if (Ext == ".dds" || Ext == ".bmp" || Ext == ".pcx" || Ext == ".tga" || Ext == ".jpg" || Ext == ".png") return ESferaResourceKind::Texture;
    if (Ext == ".def" && Path.find("sounds\\") != std::string::npos) return ESferaResourceKind::SoundDefinition;
    if (Ext == ".wav" || Ext == ".ogg" || Ext == ".mp3") return ESferaResourceKind::SoundWave;
    if (Ext == ".skl") return ESferaResourceKind::Skeleton;
    if (Ext == ".chr" || Ext == ".mdl" || Ext == ".msh") return ESferaResourceKind::Model;
    if (Ext == ".mtl" || Path.find("materials.cfg") != std::string::npos || Path.find("matbase.dat") != std::string::npos) return ESferaResourceKind::Material;
    if ((Ext == ".bin" || Ext == ".mtx") && Path.find("landscape") != std::string::npos) return ESferaResourceKind::Landscape;
    if (Path.find("shaders\\") != std::string::npos) return ESferaResourceKind::Shader;
    if (Path.find("filelist.dat") != std::string::npos || Path.find("list.dat") != std::string::npos) return ESferaResourceKind::FileList;
    if (Ext == ".exe" || Ext == ".dll") return ESferaResourceKind::ExecutableIgnored;
    return ESferaResourceKind::Unknown;
}

std::string SferaResourceManager::MakeDiskPath(const std::string& LogicalPath) const
{
    const std::string Normalized = SferaStringUtil::NormalizeLogicalPath(LogicalPath);
    if (Normalized.empty() || Normalized == ".")
    {
        return Root;
    }
    return (std::filesystem::path(Root) / Normalized).string();
}
