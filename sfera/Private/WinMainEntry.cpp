#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

extern "C" int sfera_client_winmain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand);

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand)
{
    return sfera_client_winmain(instance, previousInstance, commandLine, showCommand);
}
