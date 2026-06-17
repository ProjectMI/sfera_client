#include "SferaClientApp.h"

bool SferaClientApp::Initialize(HINSTANCE Instance, const std::string& CommandLine, int ShowCommand)
{
    Context.Instance = Instance;
    Context.ShowCommand = ShowCommand;
    Context.CommandLine.Parse(CommandLine);
    Context.Config.LoadBootstrapConfig(Context.CommandLine);

    // Original WinMain pins thread affinity to CPU 0 through SetThreadAffinityMask(GetCurrentThread(), 1).
    SetThreadAffinityMask(GetCurrentThread(), 1);

    if (!MainWindow.Register(Instance))
    {
        return false;
    }

    if (!MainWindow.Create(Instance, Context.Config.GetWindowSize(), ShowCommand))
    {
        return false;
    }

    Context.MainWindow = MainWindow.GetHandle();
    Context.Diagnostics.SetMainWindow(Context.MainWindow);

    if (!EngineLoop.Initialize(Context))
    {
        return false;
    }

    MainWindow.SetInterfaceResources(EngineLoop.GetInterfaceResources());
    return true;
}

int SferaClientApp::Run(HINSTANCE Instance, HINSTANCE, const std::string& CommandLine, int ShowCommand)
{
    if (!Initialize(Instance, CommandLine, ShowCommand))
    {
        Shutdown();
        return 1;
    }

    const int Result = EngineLoop.Run();
    Shutdown();
    return Result;
}

void SferaClientApp::Shutdown()
{
    MainWindow.SetInterfaceResources(nullptr);
    EngineLoop.Shutdown();
    MainWindow.Destroy();
    Context.MainWindow = nullptr;
}
