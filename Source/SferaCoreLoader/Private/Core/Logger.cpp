#include "Core/Logger.h"

static std::string_view ToText(ELogLevel level)
{
    switch (level)
    {
    case ELogLevel::Trace: return "trace";
    case ELogLevel::Info: return "info";
    case ELogLevel::Warning: return "warning";
    case ELogLevel::Error: return "error";
    }

    return "unknown";
}

static std::string MakeTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    #if defined(_MSC_VER)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

FLogger::FLogger(FPath logPath)
{
    Open(std::move(logPath));
}

void FLogger::Open(FPath logPath)
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (logPath.has_parent_path())
    {
        std::filesystem::create_directories(logPath.parent_path());
    }

    Stream.open(logPath, std::ios::out | std::ios::app);
}

void FLogger::Write(ELogLevel level, std::string_view message)
{
    std::lock_guard<std::mutex> lock(Mutex);
    std::string line = MakeTimestamp() + " [" + std::string(ToText(level)) + "] " + std::string(message);
    std::cout << line << '\n';

    if (Stream.is_open())
    {
        Stream << line << '\n';
    }
}
