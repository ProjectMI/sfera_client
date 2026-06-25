#pragma once
#include "Core/Types.h"

enum class ELogLevel 
{ 
    Trace, 
    Info, 
    Warning, 
    Error 
};

class FLogger 
{
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


inline double DurationLogMillisecondsSince(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

inline std::string FormatDurationLogValue(double Milliseconds)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << Milliseconds;
    return out.str();
}

inline void LogDurationProbe(FLogger* Logger, std::string_view Name, double Milliseconds, double ThresholdMilliseconds, std::string_view Detail = {})
{
    if (!Logger || Milliseconds < ThresholdMilliseconds)
    {
        return;
    }

    static std::mutex CooldownMutex;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> LastLogTime;
    const auto Now = std::chrono::steady_clock::now();
    const std::string Key(Name);
    {
        std::lock_guard<std::mutex> Lock(CooldownMutex);
        if (auto It = LastLogTime.find(Key); It != LastLogTime.end() && Now - It->second < std::chrono::milliseconds(150))
        {
            return;
        }
        LastLogTime[Key] = Now;
    }

    try
    {
        std::string Message = "stutter probe: ";
        Message += Name;
        Message += " took ";
        Message += FormatDurationLogValue(Milliseconds);
        Message += " ms";
        if (!Detail.empty())
        {
            Message += "; ";
            Message += Detail;
        }
        Logger->Warning(Message);
    }
    catch (...)
    {
    }
}


inline void LogDurationProbeEvent(FLogger* Logger, std::string_view Name, std::string_view Detail = {})
{
    if (!Logger)
    {
        return;
    }

    static std::mutex CooldownMutex;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> LastLogTime;
    const auto Now = std::chrono::steady_clock::now();
    const std::string Key(Name);
    {
        std::lock_guard<std::mutex> Lock(CooldownMutex);
        if (auto It = LastLogTime.find(Key); It != LastLogTime.end() && Now - It->second < std::chrono::milliseconds(150))
        {
            return;
        }
        LastLogTime[Key] = Now;
    }

    try
    {
        std::string Message = "stutter probe event: ";
        Message += Name;
        if (!Detail.empty())
        {
            Message += "; ";
            Message += Detail;
        }
        Logger->Info(Message);
    }
    catch (...)
    {
    }
}

class FScopedDurationLog
{
public:
    FScopedDurationLog(FLogger* InLogger, std::string_view InName, double InThresholdMilliseconds, std::string InDetail = {})
        : Logger(InLogger), Name(InName), ThresholdMilliseconds(InThresholdMilliseconds), Detail(std::move(InDetail)), Start(std::chrono::steady_clock::now())
    {
    }

    ~FScopedDurationLog()
    {
        LogDurationProbe(Logger, Name, DurationLogMillisecondsSince(Start), ThresholdMilliseconds, Detail);
    }

    FScopedDurationLog(const FScopedDurationLog&) = delete;
    FScopedDurationLog& operator=(const FScopedDurationLog&) = delete;

private:
    FLogger* Logger = nullptr;
    std::string Name;
    double ThresholdMilliseconds = 0.0;
    std::string Detail;
    std::chrono::steady_clock::time_point Start;
};
