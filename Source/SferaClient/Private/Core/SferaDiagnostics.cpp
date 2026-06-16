#include "SferaDiagnostics.h"
#include <cstdio>

void SferaDiagnostics::SetMainWindow(HWND WindowHandle)
{
    MainWindow = WindowHandle;
}

void SferaDiagnostics::Info(const char* Message)
{
    OutputDebugStringA(Message ? Message : "");
    OutputDebugStringA("\n");
}

void SferaDiagnostics::Fatal(const char* Message)
{
    // Original analogue: sub_499830 / MessageBoxA + ExitProcess path.
    MessageBoxA(MainWindow, Message ? Message : "Unknown fatal error", "Error", MB_ICONERROR | MB_OK);
}
