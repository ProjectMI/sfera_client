#include "Components/Network/ServerEventQueue.h"

void FServerEventQueue::Push(FServerEvent event)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Pending.push_back(std::move(event));
}

void FServerEventQueue::Push(std::vector<FServerEvent> events)
{
    if (events.empty()) { return; }
    std::lock_guard<std::mutex> lock(Mutex);
    Pending.reserve(Pending.size() + events.size());
    std::move(events.begin(), events.end(), std::back_inserter(Pending));
}

std::vector<FServerEvent> FServerEventQueue::Drain()
{
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<FServerEvent> result;
    result.swap(Pending);
    std::stable_sort(result.begin(), result.end(), [](const FServerEvent& left, const FServerEvent& right) { return left.Sequence < right.Sequence; });
    return result;
}

void FServerEventQueue::Clear()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Pending.clear();
}

size_t FServerEventQueue::Size() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Pending.size();
}
