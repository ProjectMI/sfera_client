#pragma once
#include "Core/Types.h"
#include <fstream>
#include <mutex>

namespace Sfera {
enum class ELogLevel { Trace, Info, Warning, Error };

class FLogger {
public:
    FLogger() = default;
    explicit FLogger(FPath logPath);
    void Open(FPath logPath);
    void Write(ELogLevel level, std::string_view message);
    void Trace(std::string_view message) { Write(ELogLevel::Trace, message); }
    void Info(std::string_view message) { Write(ELogLevel::Info, message); }
    void Warning(std::string_view message) { Write(ELogLevel::Warning, message); }
    void Error(std::string_view message) { Write(ELogLevel::Error, message); }
private:
    std::mutex Mutex;
    std::ofstream Stream;
};
}
