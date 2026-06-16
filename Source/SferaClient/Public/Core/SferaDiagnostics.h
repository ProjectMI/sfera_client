#pragma once
#include "SferaBase.h"

class SferaDiagnostics
{
public:
    void SetMainWindow(HWND WindowHandle);
    void Info(std::string_view Message);
    void Fatal(std::string_view Message);

private:
    HWND MainWindow = nullptr;
};
