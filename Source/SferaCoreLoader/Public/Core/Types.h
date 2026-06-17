#pragma once
#if !defined(_WIN64)
#error SferaCoreLoader is Win64-only. Build the project as x64 under MSVC.
#endif

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace Sfera {
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using FByteArray = std::vector<uint8>;
using FPath = std::filesystem::path;

enum class EStatusCode { Ok, NotFound, InvalidData, Unsupported, IoError, NetworkError, RuntimeError };

class FStatus {
public:
    FStatus() = default;
    FStatus(EStatusCode code, std::string message) : CodeValue(code), MessageValue(std::move(message)) {}
    static FStatus Ok() { return {}; }
    static FStatus Error(EStatusCode code, std::string message) { return FStatus(code, std::move(message)); }
    bool IsOk() const { return CodeValue == EStatusCode::Ok; }
    EStatusCode Code() const { return CodeValue; }
    const std::string& Message() const { return MessageValue; }
private:
    EStatusCode CodeValue = EStatusCode::Ok;
    std::string MessageValue;
};

template<class T>
class TResult {
public:
    TResult(T value) : ValueData(std::move(value)), StatusData(FStatus::Ok()) {}
    TResult(FStatus status) : StatusData(std::move(status)) {}
    bool IsOk() const { return StatusData.IsOk(); }
    const FStatus& Status() const { return StatusData; }
    T& Value() { return *ValueData; }
    const T& Value() const { return *ValueData; }
private:
    std::optional<T> ValueData;
    FStatus StatusData;
};
}
