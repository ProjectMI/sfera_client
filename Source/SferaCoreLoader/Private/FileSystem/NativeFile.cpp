#include "FileSystem/NativeFile.h"
#include <fstream>
#include <algorithm>

TResult<FByteArray> FNativeFile::ReadAllBytes(const FPath& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file) { return FStatus::Error(EStatusCode::NotFound, "file not found: " + path.string()); }

    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();

    if (size < 0) { return FStatus::Error(EStatusCode::IoError, "tellg failed: " + path.string()); }

    file.seekg(0, std::ios::beg);
    FByteArray bytes(static_cast<size_t>(size));

    if (!bytes.empty())
    {
        std::string buffer(bytes.size(), '\0');
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::transform(buffer.begin(), buffer.end(), bytes.begin(), [](char value)
        {
            return static_cast<uint8>(value);
        });
    }

    if (!file && !file.eof()) { return FStatus::Error(EStatusCode::IoError, "read failed: " + path.string()); }

    return bytes;
}

TResult<std::string> FNativeFile::ReadAllText(const FPath& path)
{
    auto bytes = ReadAllBytes(path);

    if (!bytes.IsOk()) { return bytes.Status(); }

    if (bytes.Value().empty()) { return std::string(); }

    std::string text;
    text.reserve(bytes.Value().size());

    for (uint8 value : bytes.Value())
    {
        text.push_back(static_cast<char>(value));
    }

    return text;
}

FStatus FNativeFile::WriteAllBytes(const FPath& path, const FByteArray& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);

    if (!file) { return FStatus::Error(EStatusCode::IoError, "open for write failed: " + path.string()); }

    if (!bytes.empty())
    {
        std::string buffer;
        buffer.reserve(bytes.size());

        for (uint8 value : bytes)
        {
            buffer.push_back(static_cast<char>(value));
        }

        file.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }

    return file ? FStatus::Ok() : FStatus::Error(EStatusCode::IoError, "write failed: " + path.string());
}

bool FNativeFile::Exists(const FPath& path) { return std::filesystem::exists(path); }
