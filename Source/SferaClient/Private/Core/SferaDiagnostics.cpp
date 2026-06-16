#include "SferaDiagnostics.h"
#include <cstdio>
#include <string>

void SferaDiagnostics::SetMainWindow(HWND WindowHandle)
{
    MainWindow = WindowHandle;
}

void SferaDiagnostics::Info(std::string_view Message)
{
    OutputDebugStringA(std::string(Message).c_str());
    OutputDebugStringA("\n");
}

void SferaDiagnostics::Fatal(std::string_view Message)
{
    // Original analogue: sub_499830 / MessageBoxA + ExitProcess path.
    MessageBoxA(MainWindow, Message.empty() ? "Unknown fatal error" : std::string(Message).c_str(), "Error", MB_ICONERROR | MB_OK);
}
