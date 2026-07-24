#pragma once
#include "Core/Types.h"

enum class EFileTransferStatus : int32
{
    Idle = 0,
    TimedOut = -3,
    Requesting = -4,
    Received = -7,
    UnpackFailed = -11,
    FileOpenFailed = -12
};

class FFileTransferStateComponent
{
public:
    bool Begin(std::string logicalName, bool keepOriginalName);
    void UpdateProgress(uint64 receivedParts, uint64 totalParts);
    void Complete();
    void Fail(EFileTransferStatus status);
    EFileTransferStatus Status() const { return CurrentStatus; }
    int32 PercentReceived() const { return ProgressPercent; }
    const std::string& RequestedName() const { return LogicalName; }
    bool KeepOriginalName() const { return PreserveName; }
private:
    std::string LogicalName;
    EFileTransferStatus CurrentStatus = EFileTransferStatus::Idle;
    int32 ProgressPercent = 0;
    bool PreserveName = false;
};
