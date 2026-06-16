#include "SferaEngineLoop.h"
#include "SferaAppContext.h"

bool SferaEngineLoop::Initialize(SferaAppContext& Context)
{
    AppContext = &Context;

    if (!ResourceManager.Initialize("."))
    {
        Context.Diagnostics.Fatal("ResourceManager initialization failed");
        return false;
    }

    ResourceManager.ScanBootstrapResources();
    Context.Config.LoadRuntimeConfig(ResourceManager);

    TextureManager.Initialize(ResourceManager);
    InterfaceResources.Initialize(ResourceManager);
    CursorManager.Initialize(ResourceManager, Context.Config.UseHardwareCursor());
    InputSystem.Initialize(Context.MainWindow);
    Renderer.Initialize(Context.MainWindow, &ResourceManager);
    SoundSystem.Initialize(ResourceManager);
    NetworkClient.Initialize();
    ScriptRuntime.Initialize(ResourceManager);
    ScriptRuntime.LoadMainProgram();

    bInitialized = true;
    return true;
}

int SferaEngineLoop::Run()
{
    if (!bInitialized)
    {
        return 1;
    }

    MSG Message = {};
    bool bRunning = true;

    while (bRunning)
    {
        while (PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if (Message.message == WM_QUIT)
            {
                bRunning = false;
                break;
            }

            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }

        InputSystem.Tick();
        NetworkClient.Tick();
        ScriptRuntime.Tick();
        InvalidateRect(AppContext->MainWindow, nullptr, FALSE);
        Renderer.Tick();
        Sleep(1);
    }

    return static_cast<int>(Message.wParam);
}

void SferaEngineLoop::Shutdown()
{
    NetworkClient.Shutdown();
    SoundSystem.Shutdown();
    ScriptRuntime.Shutdown();
    InterfaceResources.Shutdown();
    TextureManager.Shutdown();
    Renderer.Shutdown();
    InputSystem.Shutdown();
    CursorManager.Shutdown();
    ResourceManager.Shutdown();
    bInitialized = false;
}
