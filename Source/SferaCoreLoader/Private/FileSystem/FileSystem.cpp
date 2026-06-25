#include "FileSystem/FileSystem.h"
#include "FileSystem/NativeFile.h"
#include "FileSystem/PathUtils.h"

FFileSystem::FFileSystem(FPath root)
{
    SetRoot(root.empty() ? PathUtils::GetExecutableDirectory() : std::move(root));
}

void FFileSystem::SetRoot(FPath root)
{
    Root = std::filesystem::weakly_canonical(std::filesystem::absolute(std::move(root)));
    Files.clear();
    Lookup.clear();
}

void FFileSystem::BuildCatalog(FLogger* logger)
{
    Files.clear();
    Lookup.clear();
    Files.reserve(8192);
    Lookup.reserve(8192);

    if (!std::filesystem::exists(Root))
    {
        if (logger)
        {
            logger->Warning("FileSystem root does not exist: " + Root.string());
        }

        return;
    }

    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(Root, std::filesystem::directory_options::skip_permission_denied, error))
    {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || entryError) { continue; }

        FFileRecord record;
        record.AbsolutePath = entry.path();
        record.RelativePath = record.AbsolutePath.lexically_relative(Root);
        record.Size = static_cast<uint64>(entry.file_size(entryError));
        if (entryError) { record.Size = 0; }
        size_t index = Files.size();
        Files.push_back(record);
        Lookup.emplace(PathUtils::NormalizeForLookup(record.RelativePath), index);
    }

    if (logger)
    {
        logger->Info("FileSystem cataloged " + std::to_string(Files.size()) + " files under executable directory");
    }
}

std::optional<FPath> FFileSystem::Resolve(std::string_view logicalPath) const
{
    FPath input
    {
        std::string(logicalPath)
    };

    if (input.is_absolute() && std::filesystem::exists(input)) { return input; }

    std::string key = PathUtils::NormalizeForLookup(input);
    auto it = Lookup.find(key);

    if (it != Lookup.end()) { return Files[it->second].AbsolutePath; }

    FPath candidate = Root / input;

    if (std::filesystem::exists(candidate)) { return candidate; }

    return std::nullopt;
}

TResult<FByteArray> FFileSystem::ReadBytes(std::string_view logicalPath) const
{
    auto path = Resolve(logicalPath);

    if (!path) { return FStatus::Error(EStatusCode::NotFound, "logical file not found under executable directory: " + std::string(logicalPath)); }

    return FNativeFile::ReadAllBytes(*path);
}

TResult<std::string> FFileSystem::ReadText(std::string_view logicalPath) const
{
    auto path = Resolve(logicalPath);

    if (!path) { return FStatus::Error(EStatusCode::NotFound, "logical text file not found under executable directory: " + std::string(logicalPath)); }

    return FNativeFile::ReadAllText(*path);
}
