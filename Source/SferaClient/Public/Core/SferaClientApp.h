#pragma once
#include "SferaAppContext.h"
#include "SferaWindow.h"
#include "SferaEngineLoop.h"

class SferaClientApp
{
public:
    int Run(HINSTANCE Instance, HINSTANCE PreviousInstance, LPSTR CommandLine, int ShowCommand);

private:
    bool Initialize(HINSTANCE Instance, LPSTR CommandLine, int ShowCommand);
    void Shutdown();

private:
    SferaAppContext Context;
    SferaWindow MainWindow;
    SferaEngineLoop EngineLoop;
};
