#include "Components/Network/FileTransferStateComponent.h"

bool FFileTransferStateComponent::Begin(std::string logicalName, bool keepOriginalName)
{
    if (CurrentStatus == EFileTransferStatus::Requesting || logicalName.empty()) { return false; }
    LogicalName = std::move(logicalName);
    PreserveName = keepOriginalName;
    ProgressPercent = 0;
    CurrentStatus = EFileTransferStatus::Requesting;
    return true;
}

void FFileTransferStateComponent::UpdateProgress(uint64 receivedParts, uint64 totalParts)
{
    if (CurrentStatus != EFileTransferStatus::Requesting || totalParts == 0) { return; }
    const uint64 clamped = std::min(receivedParts, totalParts);
    ProgressPercent = static_cast<int32>((clamped * 100) / totalParts);
}

void FFileTransferStateComponent::Complete()
{
    ProgressPercent = 100;
    CurrentStatus = EFileTransferStatus::Received;
}

void FFileTransferStateComponent::Fail(EFileTransferStatus status)
{
    if (status == EFileTransferStatus::Requesting || status == EFileTransferStatus::Received) { return; }
    CurrentStatus = status;
}
