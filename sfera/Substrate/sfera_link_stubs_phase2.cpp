/*
   sfera_link_stubs_phase2.cpp
   C++ import resolver for the recovered 32-bit client.
   Keep this file as link glue only; new behavior belongs in typed services/classes.
*/
#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_DEPRECATE 1
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <malloc.h>
#include <stdio.h>

#if defined(_MSC_VER)
#pragma warning(disable:4028 4047 4090 4113 4133 4244 4311 4312 4996 4700 4701 4703)
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#ifndef WINAPI
#define WINAPI __stdcall
#endif

typedef uintptr_t ida_u;
typedef intptr_t ida_i;
typedef long long ida_i64;
typedef unsigned long long ida_u64;

extern "C" __declspec(dllimport) void* WINAPI LoadLibraryA(const char* name);
extern "C" __declspec(dllimport) void* WINAPI GetProcAddress(void* module, const char* name);

static void* ida_resolve(const char* dll_name, const char* proc_name)
{
    void* m = LoadLibraryA(dll_name);
    return m ? GetProcAddress(m, proc_name) : 0;
}

namespace sfera::client::link_imports
{
    template <typename... Args>
    ida_u invokeImportedProcedure(void*& cachedProcedure, const char* dllName, const char* procedureName, Args... args)
    {
        using Procedure = ida_u(WINAPI*)(Args...);
        if (!cachedProcedure)
            cachedProcedure = ida_resolve(dllName, procedureName);
        return cachedProcedure ? reinterpret_cast<Procedure>(cachedProcedure)(args...) : 0;
    }
}

extern "C"
{

/* Cdecl import thunks for stdcall WinAPI/COM/Winsock names emitted by Hex-Rays. */
ida_u __cdecl GetLocalTime(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetLocalTime", a1);
}
ida_u __cdecl closesocket(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "closesocket", a1);
}
ida_u __cdecl WSACleanup()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "WSACleanup");
}
ida_u __cdecl send(ida_u a1, ida_u a2, ida_u a3, ida_u a4)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "send", a1, a2, a3, a4);
}
ida_u __cdecl WSAGetLastError()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "WSAGetLastError");
}
ida_u __cdecl select(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "select", a1, a2, a3, a4, a5);
}
ida_u __cdecl _WSAFDIsSet(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "__WSAFDIsSet", a1, a2);
}
ida_u __cdecl recv(ida_u a1, ida_u a2, ida_u a3, ida_u a4)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "recv", a1, a2, a3, a4);
}
ida_u __cdecl WSAStartup(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "WSAStartup", a1, a2);
}
ida_u __cdecl socket(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "socket", a1, a2, a3);
}
ida_u __cdecl setsockopt(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "setsockopt", a1, a2, a3, a4, a5);
}
ida_u __cdecl htons(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "htons", a1);
}
ida_u __cdecl inet_addr(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "inet_addr", a1);
}
ida_u __cdecl gethostbyname(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "gethostbyname", a1);
}
ida_u __cdecl inet_ntoa(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "inet_ntoa", a1);
}
ida_u __cdecl connect(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "connect", a1, a2, a3);
}
ida_u __cdecl SetThreadPriority(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "SetThreadPriority", a1, a2);
}
ida_u __cdecl SetPixel(ida_u a1, ida_u a2, ida_u a3, ida_u a4)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "SetPixel", a1, a2, a3, a4);
}
ida_u __cdecl SelectObject(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "SelectObject", a1, a2);
}
ida_u __cdecl DeleteDC(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "DeleteDC", a1);
}
ida_u __cdecl GetDIBits(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6, ida_u a7)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "GetDIBits", a1, a2, a3, a4, a5, a6, a7);
}
ida_u __cdecl CreateCompatibleDC(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "CreateCompatibleDC", a1);
}
ida_u __cdecl CreateCompatibleBitmap(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "CreateCompatibleBitmap", a1, a2, a3);
}
ida_u __cdecl UnmapViewOfFile(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "UnmapViewOfFile", a1);
}
ida_u __cdecl GetExitCodeThread(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetExitCodeThread", a1, a2);
}
ida_u __cdecl GetWindowLongA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetWindowLongA", a1, a2);
}
ida_u __cdecl WideCharToMultiByte(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6, ida_u a7, ida_u a8)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "WideCharToMultiByte", a1, a2, a3, a4, a5, a6, a7, a8);
}
ida_u __cdecl SysFreeString(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "oleaut32.dll", "SysFreeString", a1);
}
ida_u __cdecl VariantInit(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "oleaut32.dll", "VariantInit", a1);
}
ida_u __cdecl MultiByteToWideChar(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "MultiByteToWideChar", a1, a2, a3, a4, a5, a6);
}
ida_u __cdecl GlobalAlloc(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GlobalAlloc", a1, a2);
}
ida_u __cdecl SysAllocString(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "oleaut32.dll", "SysAllocString", a1);
}
ida_u __cdecl GlobalFree(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GlobalFree", a1);
}
ida_u __cdecl VariantClear(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "oleaut32.dll", "VariantClear", a1);
}
ida_u __cdecl OleCreate(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6, ida_u a7)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ole32.dll", "OleCreate", a1, a2, a3, a4, a5, a6, a7);
}
ida_u __cdecl SetWindowLongA(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetWindowLongA", a1, a2, a3);
}
ida_u __cdecl OleSetContainedObject(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ole32.dll", "OleSetContainedObject", a1, a2);
}
ida_u __cdecl timeGetTime()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "winmm.dll", "timeGetTime");
}
ida_u __cdecl CreateToolhelp32Snapshot(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "CreateToolhelp32Snapshot", a1, a2);
}
ida_u __cdecl Process32First(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "Process32First", a1, a2);
}
ida_u __cdecl Process32Next(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "Process32Next", a1, a2);
}
ida_u __cdecl bind(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "bind", a1, a2, a3);
}
ida_u __cdecl CoCreateInstance(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ole32.dll", "CoCreateInstance", a1, a2, a3, a4, a5);
}
ida_u __cdecl GetVolumeInformationA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6, ida_u a7, ida_u a8)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetVolumeInformationA", a1, a2, a3, a4, a5, a6, a7, a8);
}
ida_u __cdecl ShellExecuteA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "shell32.dll", "ShellExecuteA", a1, a2, a3, a4, a5, a6);
}
ida_u __cdecl ShowCursor(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "ShowCursor", a1);
}
ida_u __cdecl MessageBeep(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "MessageBeep", a1);
}
ida_u __cdecl UnregisterClassA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "UnregisterClassA", a1, a2);
}
ida_u __cdecl CoUninitialize()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ole32.dll", "CoUninitialize");
}
ida_u __cdecl GetSystemMetrics(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetSystemMetrics", a1);
}
ida_u __cdecl SetRect(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetRect", a1, a2, a3, a4, a5);
}
ida_u __cdecl InvalidateRect(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "InvalidateRect", a1, a2, a3);
}
ida_u __cdecl BringWindowToTop(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "BringWindowToTop", a1);
}
ida_u __cdecl CreateFileMappingA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "CreateFileMappingA", a1, a2, a3, a4, a5, a6);
}
ida_u __cdecl MapViewOfFile(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "MapViewOfFile", a1, a2, a3, a4, a5);
}
ida_u __cdecl DirectInput8Create(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "dinput8.dll", "DirectInput8Create", a1, a2, a3, a4, a5);
}
ida_u __cdecl CoInitialize(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ole32.dll", "CoInitialize", a1);
}
ida_u __cdecl InitCommonControls()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "comctl32.dll", "InitCommonControls");
}
ida_u __cdecl FindWindowA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "FindWindowA", a1, a2);
}
ida_u __cdecl GetSystemDirectoryA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetSystemDirectoryA", a1, a2);
}
ida_u __cdecl CreateProcessA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5, ida_u a6, ida_u a7, ida_u a8, ida_u a9, ida_u a10)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "CreateProcessA", a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}
ida_u __cdecl TerminateProcess(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "TerminateProcess", a1, a2);
}
ida_u __cdecl GetExitCodeProcess(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetExitCodeProcess", a1, a2);
}
ida_u __cdecl gethostbyaddr(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "gethostbyaddr", a1, a2, a3);
}
ida_u __cdecl ioctlsocket(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "ws2_32.dll", "ioctlsocket", a1, a2, a3);
}
ida_u __cdecl SendMessageA(ida_u a1, ida_u a2, ida_u a3, ida_u a4)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SendMessageA", a1, a2, a3, a4);
}
ida_u __cdecl CallWindowProcA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "CallWindowProcA", a1, a2, a3, a4, a5);
}
ida_u __cdecl ClientToScreen(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "ClientToScreen", a1, a2);
}
ida_u __cdecl SetFocus(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetFocus", a1);
}
ida_u __cdecl IsClipboardFormatAvailable(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "IsClipboardFormatAvailable", a1);
}
ida_u __cdecl OpenClipboard(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "OpenClipboard", a1);
}
ida_u __cdecl GetClipboardData(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetClipboardData", a1);
}
ida_u __cdecl GlobalLock(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GlobalLock", a1);
}
ida_u __cdecl GlobalUnlock(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GlobalUnlock", a1);
}
ida_u __cdecl CloseClipboard()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "CloseClipboard");
}
ida_u __cdecl GetDlgItem(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetDlgItem", a1, a2);
}
ida_u __cdecl IsDialogMessageA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "IsDialogMessageA", a1, a2);
}
ida_u __cdecl EnableWindow(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "EnableWindow", a1, a2);
}
ida_u __cdecl GetWindowTextA(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetWindowTextA", a1, a2, a3);
}
ida_u __cdecl GetDlgCtrlID(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetDlgCtrlID", a1);
}
ida_u __cdecl CreateDialogParamA(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "CreateDialogParamA", a1, a2, a3, a4, a5);
}
ida_u __cdecl SetClassLongA(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetClassLongA", a1, a2, a3);
}
ida_u __cdecl lstrlenA(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "lstrlenA", a1);
}
ida_u __cdecl wvsprintfA(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "wvsprintfA", a1, a2, a3);
}
ida_u __cdecl OutputDebugStringA(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "OutputDebugStringA", a1);
}
ida_u __cdecl GetCurrentThreadId()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetCurrentThreadId");
}
ida_u __cdecl GetCurrentProcessId()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetCurrentProcessId");
}
ida_u __cdecl GetCurrentProcess()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetCurrentProcess");
}
ida_u __cdecl RaiseException(ida_u a1, ida_u a2, ida_u a3, ida_u a4)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "RaiseException", a1, a2, a3, a4);
}
ida_u __cdecl FileTimeToLocalFileTime(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "FileTimeToLocalFileTime", a1, a2);
}
ida_u __cdecl FileTimeToDosDateTime(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "FileTimeToDosDateTime", a1, a2, a3);
}
ida_u __cdecl GetSystemTimeAsFileTime(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetSystemTimeAsFileTime", a1);
}
ida_u __cdecl lstrcpyA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "lstrcpyA", a1, a2);
}
ida_u __cdecl GetUserNameA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "advapi32.dll", "GetUserNameA", a1, a2);
}
ida_u __cdecl GetSystemInfo(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetSystemInfo", a1);
}
ida_u __cdecl GlobalMemoryStatus(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GlobalMemoryStatus", a1);
}
ida_u __cdecl VirtualQuery(ida_u a1, ida_u a2, ida_u a3)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "VirtualQuery", a1, a2, a3);
}
ida_u __cdecl IsDebuggerPresent()
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "IsDebuggerPresent");
}
ida_u __cdecl lstrcatA(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "lstrcatA", a1, a2);
}
ida_u __cdecl GetVersionExA(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "GetVersionExA", a1);
}
ida_u __cdecl SetUnhandledExceptionFilter(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "SetUnhandledExceptionFilter", a1);
}
ida_u __cdecl CreateBitmap(ida_u a1, ida_u a2, ida_u a3, ida_u a4, ida_u a5)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "CreateBitmap", a1, a2, a3, a4, a5);
}
ida_u __cdecl GetObjectType(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "GetObjectType", a1);
}
ida_u __cdecl CreateIconIndirect(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "gdi32.dll", "CreateIconIndirect", a1);
}
ida_u __cdecl DestroyCursor(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "DestroyCursor", a1);
}
ida_u __cdecl ClipCursor(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "ClipCursor", a1);
}
ida_u __cdecl SetCursor(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetCursor", a1);
}
ida_u __cdecl GetCursorPos(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "GetCursorPos", a1);
}
ida_u __cdecl ScreenToClient(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "ScreenToClient", a1, a2);
}
ida_u __cdecl SetCursorPos(ida_u a1, ida_u a2)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "user32.dll", "SetCursorPos", a1, a2);
}
ida_u __cdecl TryEnterCriticalSection(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "TryEnterCriticalSection", a1);
}
ida_u __cdecl IsProcessorFeaturePresent(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "IsProcessorFeaturePresent", a1);
}
ida_u __cdecl LocalFree(ida_u a1)
{
    static void* cachedProcedure = 0;
    return sfera::client::link_imports::invokeImportedProcedure(cachedProcedure, "kernel32.dll", "LocalFree", a1);
}

/* CRT compatibility wrappers where decompiler dropped/changed the leading underscore. */
__time64_t __cdecl time64(__time64_t* t) { return _time64(t); }
struct tm* __cdecl localtime64(const __time64_t* t) { return _localtime64(t); }
char* __cdecl strtime(char* buf) { return _strtime(buf); }
size_t __cdecl msize(void* p) { return _msize(p); }
intptr_t __cdecl findfirst64i32(const char* name, struct _finddata64i32_t* fd) { return _findfirst64i32(name, fd); }
int __cdecl findnext64i32(intptr_t h, struct _finddata64i32_t* fd) { return _findnext64i32(h, fd); }
int __cdecl fstat64i32(int fd, struct _stat64i32* st) { return _fstat64i32(fd, st); }
int __cdecl stat64i32(const char* path, struct _stat64i32* st) { return _stat64i32(path, st); }
int __cdecl utime64(const char* path, struct __utimbuf64* tb) { return _utime64(path, tb); }
int __cdecl futime64(int fd, struct __utimbuf64* tb) { return _futime64(fd, tb); }
__time64_t __cdecl mktime64(struct tm* tmv) { return _mktime64(tmv); }
double __cdecl difftime64(__time64_t a, __time64_t b) { return _difftime64(a, b); }
__int64 __cdecl atoi64(const char* s) { return _atoi64(s); }
int __cdecl vscprintf(const char* fmt, va_list ap) { return _vscprintf(fmt, ap); }
int __cdecl fseeki64(FILE* f, __int64 off, int whence) { return _fseeki64(f, off, whence); }
void __cdecl lock_file(FILE* f) { _lock_file(f); }
void __cdecl unlock_file(FILE* f) { _unlock_file(f); }

/* IDA/Hex-Rays helper artifacts. */
int __cdecl COERCE_INT(float x) { int r = 0; memcpy(&r, &x, sizeof(r)); return r; }
int __cdecl SLOBYTE(int x) { return (signed char)x; }
unsigned __int64 __cdecl __SPAIR64__(int hi, unsigned int lo) { return (((unsigned __int64)(unsigned int)hi) << 32) | lo; }
void* __cdecl memset32(void* dst, unsigned int value, size_t dword_count) { unsigned int* p = (unsigned int*)dst; while (dword_count--) *p++ = value; return dst; }
int __cdecl abs8(char x) { return x < 0 ? -x : x; }
int __cdecl abs32(int x) { return x < 0 ? -x : x; }
ida_u __cdecl unknown_libname_1(void) { return 0; }
ida_u __cdecl unknown_libname_2(void) { return 0; }
ida_u __cdecl unknown_libname_3(void) { return 0; }
int __cdecl DllMain(void) { return 1; }

/* Console CRT entry is implemented in Private/ConsoleEntry.cpp. */

/* Flattened C++/MSVC runtime names from decompiler. Compile-only stubs. */
void __cdecl CxxThrowException(void* obj, void* info) { (void)obj; (void)info; abort(); }
int _RTC_NumErrors_0 = 0;
int _cfltcvt_init_0(void) { return 0; }
ida_u __cdecl std_exception_exception(void) { return 0; }
ida_u __cdecl Concurrency_details_NonReentrantLock_Release(void) { return 0; }
ida_u __cdecl std_Xout_of_range(void) { return 0; }
ida_u __cdecl std_Xlength_error(void) { return 0; }
ida_u __cdecl std_locale_Getgloballocale(void) { return 0; }
ida_u __cdecl std_codecvt_char_char_int_Getcat(void) { return 0; }
ida_u __cdecl std_locale_facet_Incref(void) { return 0; }
ida_u __cdecl std_locale_facet_Facet_Register(void) { return 0; }
ida_u __cdecl std_filebuf_Set_back(void) { return 0; }
ida_u __cdecl std_streambuf_Init(void) { return 0; }
ida_u __cdecl std_codecvt_base_always_noconv(void) { return 0; }
ida_u __cdecl std_Fiopen(void) { return 0; }
ida_u __cdecl std_streambuf_getloc(void) { return 0; }
ida_u __cdecl std_locale_facet_Decref(void) { return 0; }
ida_u __cdecl std_uncaught_exception(void) { return 0; }
ida_u __cdecl std_ostream_Osfx(void) { return 0; }
ida_u __cdecl std_ostream_flush(void) { return 0; }
ida_u __cdecl std_streambuf_sputc(void) { return 0; }
ida_u __cdecl std_streambuf_sputn(void) { return 0; }
ida_u __cdecl std_ios_setstate(void) { return 0; }
ida_u __cdecl std_streambuf_setg(void) { return 0; }
ida_u __cdecl std_codecvt_char_char_int_out(void) { return 0; }
ida_u __cdecl std_codecvt_char_char_int_in(void) { return 0; }
ida_u __cdecl std_codecvt_char_char_int_unshift(void) { return 0; }
ida_u __cdecl std_istream_Ipfx(void) { return 0; }
ida_u __cdecl std_streambuf_sgetc(void) { return 0; }
ida_u __cdecl std_streambuf_snextc(void) { return 0; }
ida_u __cdecl std_streambuf_sbumpc(void) { return 0; }
ida_u __cdecl std_filebuf_Reset_back(void) { return 0; }
ida_u __cdecl std_ostream_ostream(void) { return 0; }
ida_u __cdecl std_streambuf_streambuf(void) { return 0; }
ida_u __cdecl std_streambuf_dtor_streambuf_char_std_char_traits_char(void) { return 0; }
ida_u __cdecl std_istream_istream(void) { return 0; }
ida_u __cdecl std_ios_base_Ios_base_dtor(void) { return 0; }
ida_u __cdecl std_ios_clear(void) { return 0; }
ida_u __cdecl std_streambuf_Pninc(void) { return 0; }
ida_u __cdecl Concurrency_details_SchedulerBase_GetSchedulerProxy(void) { return 0; }

/* Missing weak/internal functions. */
ida_u __cdecl sub_4715E0(void) { return 0; }
void __cdecl sub_465790(void* p) { (void)p; }
ida_u __cdecl sub_477900(void) { return 0; }
double __cdecl sub_47AF10(void) { return 0.0; }
ida_u __cdecl sub_482330(void) { return 0; }
void __cdecl sub_4D8580(void* p) { (void)p; }

/* Missing nullsub_* weak functions. */
ida_u __cdecl nullsub_1(void) { return 0; }
ida_u __cdecl nullsub_91(void) { return 0; }
ida_u __cdecl nullsub_6(void) { return 0; }
ida_u __cdecl nullsub_7(void) { return 0; }
ida_u __cdecl nullsub_8(void) { return 0; }
ida_u __cdecl nullsub_9(void) { return 0; }
ida_u __cdecl nullsub_10(void) { return 0; }
ida_u __cdecl nullsub_11(void) { return 0; }
ida_u __cdecl nullsub_12(void) { return 0; }
ida_u __cdecl nullsub_13(void) { return 0; }
ida_u __cdecl nullsub_14(void) { return 0; }
ida_u __cdecl nullsub_15(void) { return 0; }
ida_u __cdecl nullsub_16(void) { return 0; }
ida_u __cdecl nullsub_17(void) { return 0; }
ida_u __cdecl nullsub_18(void) { return 0; }
ida_u __cdecl nullsub_19(void) { return 0; }
ida_u __cdecl nullsub_20(void) { return 0; }
ida_u __cdecl nullsub_21(void) { return 0; }
ida_u __cdecl nullsub_22(void) { return 0; }
ida_u __cdecl nullsub_23(void) { return 0; }
ida_u __cdecl nullsub_24(void) { return 0; }
ida_u __cdecl nullsub_25(void) { return 0; }
ida_u __cdecl nullsub_26(void) { return 0; }
ida_u __cdecl nullsub_27(void) { return 0; }
ida_u __cdecl nullsub_28(void) { return 0; }
ida_u __cdecl nullsub_29(void) { return 0; }
ida_u __cdecl nullsub_30(void) { return 0; }
ida_u __cdecl nullsub_31(void) { return 0; }
ida_u __cdecl nullsub_32(void) { return 0; }
ida_u __cdecl nullsub_33(void) { return 0; }
ida_u __cdecl nullsub_34(void) { return 0; }
ida_u __cdecl nullsub_35(void) { return 0; }
ida_u __cdecl nullsub_36(void) { return 0; }
ida_u __cdecl nullsub_37(void) { return 0; }
ida_u __cdecl nullsub_38(void) { return 0; }
ida_u __cdecl nullsub_39(void) { return 0; }
ida_u __cdecl nullsub_40(void) { return 0; }
ida_u __cdecl nullsub_41(void) { return 0; }
ida_u __cdecl nullsub_42(void) { return 0; }
ida_u __cdecl nullsub_43(void) { return 0; }
ida_u __cdecl nullsub_44(void) { return 0; }
ida_u __cdecl nullsub_45(void) { return 0; }
ida_u __cdecl nullsub_46(void) { return 0; }
ida_u __cdecl nullsub_47(void) { return 0; }
ida_u __cdecl nullsub_48(void) { return 0; }
ida_u __cdecl nullsub_49(void) { return 0; }
ida_u __cdecl nullsub_50(void) { return 0; }
ida_u __cdecl nullsub_51(void) { return 0; }
ida_u __cdecl nullsub_52(void) { return 0; }
ida_u __cdecl nullsub_53(void) { return 0; }
ida_u __cdecl nullsub_54(void) { return 0; }
ida_u __cdecl nullsub_55(void) { return 0; }
ida_u __cdecl nullsub_56(void) { return 0; }
ida_u __cdecl nullsub_57(void) { return 0; }
ida_u __cdecl nullsub_58(void) { return 0; }
ida_u __cdecl nullsub_59(void) { return 0; }
ida_u __cdecl nullsub_60(void) { return 0; }
ida_u __cdecl nullsub_61(void) { return 0; }
ida_u __cdecl nullsub_62(void) { return 0; }
ida_u __cdecl nullsub_63(void) { return 0; }
ida_u __cdecl nullsub_64(void) { return 0; }
ida_u __cdecl nullsub_65(void) { return 0; }
ida_u __cdecl nullsub_66(void) { return 0; }
ida_u __cdecl nullsub_67(void) { return 0; }
ida_u __cdecl nullsub_68(void) { return 0; }
ida_u __cdecl nullsub_69(void) { return 0; }
ida_u __cdecl nullsub_70(void) { return 0; }
ida_u __cdecl nullsub_71(void) { return 0; }
ida_u __cdecl nullsub_72(void) { return 0; }
ida_u __cdecl nullsub_73(void) { return 0; }
ida_u __cdecl nullsub_74(void) { return 0; }
ida_u __cdecl nullsub_75(void) { return 0; }
ida_u __cdecl nullsub_76(void) { return 0; }
ida_u __cdecl nullsub_77(void) { return 0; }
ida_u __cdecl nullsub_78(void) { return 0; }
ida_u __cdecl nullsub_79(void) { return 0; }
ida_u __cdecl nullsub_80(void) { return 0; }
ida_u __cdecl nullsub_81(void) { return 0; }
ida_u __cdecl nullsub_82(void) { return 0; }
ida_u __cdecl nullsub_83(void) { return 0; }
ida_u __cdecl nullsub_84(void) { return 0; }
ida_u __cdecl nullsub_85(void) { return 0; }
ida_u __cdecl nullsub_86(void) { return 0; }
ida_u __cdecl nullsub_87(void) { return 0; }
ida_u __cdecl nullsub_88(void) { return 0; }
ida_u __cdecl nullsub_89(void) { return 0; }
ida_u __cdecl nullsub_90(void) { return 0; }

/* Missing global storage. Zero-initialized compile-only placeholders. */
unsigned int dword_6A2FC0 = 0;
unsigned char byte_861C08 = 0;
unsigned char byte_861C09 = 0;
unsigned char byte_861C0A = 0;
unsigned int dword_9C65B4 = 0;
unsigned int dword_9C65B8 = 0;
unsigned int dword_9C65BC = 0;
unsigned int dword_9C65C0 = 0;
unsigned int dword_9C65DC = 0;
unsigned int dword_9C65E0 = 0;
unsigned int dword_9C65F4 = 0;
unsigned int dword_9C6604 = 0;
unsigned int dword_9C6624 = 0;
unsigned int dword_9C6638 = 0;
unsigned int dword_9C663C = 0;
unsigned short word_9C6844 = 0;
unsigned int dword_9C6854 = 0;
unsigned int dword_3E5EEF8 = 0;
unsigned int dword_3E5EEFC = 0;
unsigned int dword_3E6D1A8 = 0;
unsigned int dword_3E711E8 = 0;
unsigned int dword_3E711EC = 0;
unsigned int dword_3E711F0 = 0;
unsigned int dword_3E711F4 = 0;
unsigned int dword_3E711F8 = 0;
unsigned int dword_3E711FC = 0;
unsigned int dword_3E71200 = 0;
unsigned int dword_3E71204 = 0;
unsigned int dword_3E71208 = 0;
unsigned int dword_3E71210 = 0;
unsigned int dword_3E71214 = 0;
unsigned int dword_3E71218 = 0;
unsigned int dword_3E7121C = 0;
unsigned int dword_3E71220 = 0;
unsigned char byte_3E73207 = 0;
unsigned short word_4A2AA90 = 0;
unsigned int dword_4A3F770 = 0;
float flt_4A439A8 = 0.0f;
unsigned int dword_4A43D3C = 0;
unsigned char byte_4A43F30 = 0;
unsigned int dword_4A46140 = 0;
unsigned int dword_4A46170 = 0;
unsigned short word_4A46350 = 0;
unsigned int dword_4A46354 = 0;
unsigned int dword_4A47228 = 0;
unsigned int dword_4A4EBB0 = 0;
unsigned int dword_4A8B630 = 0;
unsigned char byte_4A8B634 = 0;
unsigned int dword_4A8B638 = 0;
unsigned int dword_4A8B63C = 0;
unsigned int dword_4A8B950 = 0;
unsigned char byte_4A8C2F7 = 0;
unsigned char byte_4A8C508 = 0;
unsigned int dword_4A9B038 = 0;
float flt_4A9B03C = 0.0f;
float flt_4A9B088 = 0.0f;
float flt_4A9B08C = 0.0f;
float flt_4A9B0D8 = 0.0f;
float flt_4A9B0DC = 0.0f;
unsigned short word_4A9B130 = 0;
unsigned short word_4A9B132 = 0;
unsigned short word_4A9B134 = 0;
unsigned short word_4A9B136 = 0;
unsigned short word_4A9B138 = 0;
unsigned short word_4A9C500 = 0;
unsigned short word_4A9C502 = 0;
unsigned short word_4A9C504 = 0;
unsigned short word_4A9C506 = 0;
unsigned short word_4A9C508 = 0;
float flt_4A9C9A8 = 0.0f;
float flt_4A9C9F8 = 0.0f;
float flt_4A9CA48 = 0.0f;
unsigned char byte_4A9CABC = 0;
unsigned int dword_4AC05C0 = 0;
unsigned short word_4AE09FC = 0;
unsigned int dword_4AE0A08 = 0;
float flt_4AE0A0C = 0.0f;
float flt_4AE0A10 = 0.0f;
unsigned int dword_4AE1A0C = 0;
unsigned int dword_4AE7100 = 0;
float flt_4AE7458 = 0.0f;
unsigned char byte_4AE769F = 0;
unsigned char byte_4AE779F = 0;
unsigned int dword_4B28578 = 0;
unsigned int dword_4B335FC = 0;
unsigned int dword_4B336DC = 0;
float flt_4B336E0 = 0.0f;
float flt_4B336E4 = 0.0f;
float flt_4B336E8 = 0.0f;
unsigned int dword_4B336EC = 0;
unsigned int dword_4B336F0 = 0;
unsigned int dword_4B336F4 = 0;
float flt_4B33824 = 0.0f;
float flt_4B33830 = 0.0f;
float flt_4B33834 = 0.0f;
float flt_4B33838 = 0.0f;
float flt_4B41DB0 = 0.0f;
float flt_4B41DB4 = 0.0f;
float flt_4B41DB8 = 0.0f;
float flt_4B41DBC = 0.0f;
unsigned int dword_4B41DC0 = 0;
float flt_4B41DC8 = 0.0f;
float flt_4B41DCC = 0.0f;
float flt_4B41DD0 = 0.0f;
float flt_4B41DD4 = 0.0f;
float flt_4B41DD8 = 0.0f;
float flt_4B41DDC = 0.0f;
unsigned int dword_4B41DE0 = 0;
float flt_4B41DE8 = 0.0f;
float flt_4B41DEC = 0.0f;
float flt_4B41DF0 = 0.0f;
float flt_4B41DF4 = 0.0f;
float flt_4B41DF8 = 0.0f;
float flt_4B41DFC = 0.0f;
unsigned int dword_4B41E00 = 0;
float flt_4B41E08 = 0.0f;
float flt_4B41E0C = 0.0f;
float flt_4B41E10 = 0.0f;
float flt_4B41E14 = 0.0f;
float flt_4B41E18 = 0.0f;
float flt_4B41E1C = 0.0f;
unsigned int dword_4B41E20 = 0;
float flt_4B41E28 = 0.0f;
float flt_4B41E2C = 0.0f;
unsigned int dword_4B4B7B0 = 0;
float flt_4B4B950 = 0.0f;
float flt_4B4B9E8 = 0.0f;
unsigned int dword_4B4EC58 = 0;
unsigned int dword_4B4F188 = 0;
unsigned char byte_4B54837 = 0;
unsigned int dword_4B58D10 = 0;
unsigned int dword_4B58D14 = 0;
unsigned int dword_4B58D18 = 0;
unsigned int dword_4B59910 = 0;
unsigned short word_4B86008 = 0;
unsigned __int64 qword_4BA6268 = 0;
unsigned int dword_4BA6620 = 0;
unsigned short word_4BA7880 = 0;
unsigned short word_4BA7882 = 0;
unsigned short word_4BA7884 = 0;
unsigned short word_4BA7886 = 0;
unsigned short word_4BA7888 = 0;
char* ArgList = 0;
unsigned int dword_4BA9774 = 0;

}
