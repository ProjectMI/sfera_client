#include "SferaClientApp.h"

int WINAPI WinMain(HINSTANCE Instance, HINSTANCE PreviousInstance, LPSTR CommandLine, int ShowCommand)
{
    // Port target: original _WinMain@16 at 0x004BD210.
    SferaClientApp App;
    return App.Run(Instance, PreviousInstance, CommandLine, ShowCommand);
}
