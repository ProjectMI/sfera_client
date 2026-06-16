#pragma once
#include "SferaBase.h"
#include "SferaCommandLine.h"
#include "SferaConfig.h"
#include "SferaDiagnostics.h"

class SferaAppContext
{
public:
    HINSTANCE Instance = nullptr;        // original global hint: hInstance @ 0x04A9AFC4
    HWND MainWindow = nullptr;           // original global hint: hWnd @ 0x04AE79DC
    int ShowCommand = SW_SHOWNORMAL;

    SferaCommandLine CommandLine;
    SferaConfig Config;
    SferaDiagnostics Diagnostics;
};
