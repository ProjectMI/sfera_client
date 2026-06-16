#pragma once
#include "SferaBase.h"

class SferaNetworkClient
{
public:
    bool Initialize();
    void Shutdown();
    void Tick();
};
