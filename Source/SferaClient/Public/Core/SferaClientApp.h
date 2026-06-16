#pragma once
#include "SferaAppContext.h"
#include "SferaWindow.h"
#include "SferaEngineLoop.h"

class SferaClientApp
{
public:
    int Run(HINSTANCE Instance, HINSTANCE PreviousInstance, const std::string& CommandLine, int ShowCommand);

private:
    bool Initialize(HINSTANCE Instance, const std::string& CommandLine, int ShowCommand);
    void Shutdown();

private:
    SferaAppContext Context;
    SferaWindow MainWindow;
    SferaEngineLoop EngineLoop;
};
