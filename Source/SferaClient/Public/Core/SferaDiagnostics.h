#pragma once
#include "SferaBase.h"

class SferaDiagnostics
{
public:
    void SetMainWindow(HWND WindowHandle);
    void Info(const char* Message);
    void Fatal(const char* Message);

private:
    HWND MainWindow = nullptr;
};
