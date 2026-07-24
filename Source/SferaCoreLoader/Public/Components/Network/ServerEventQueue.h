#pragma once
#include "Components/Network/ServerEvent.h"

class FServerEventQueue
{
public:
    void Push(FServerEvent event);
    void Push(std::vector<FServerEvent> events);
    std::vector<FServerEvent> Drain();
    void Clear();
    size_t Size() const;
private:
    mutable std::mutex Mutex;
    std::vector<FServerEvent> Pending;
};
