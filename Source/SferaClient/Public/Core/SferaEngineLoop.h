#pragma once
#include "SferaBase.h"
#include "SferaRenderer.h"
#include "SferaInputSystem.h"
#include "SferaCursorManager.h"
#include "SferaResourceManager.h"
#include "SferaTextureManager.h"
#include "SferaInterfaceResourceManager.h"
#include "SferaScriptRuntime.h"
#include "SferaSoundSystem.h"
#include "SferaNetworkClient.h"

class SferaAppContext;

class SferaEngineLoop
{
public:
    bool Initialize(SferaAppContext& Context);
    int Run();
    void Shutdown();
    const SferaInterfaceResourceManager* GetInterfaceResources() const { return &InterfaceResources; }

private:
    SferaAppContext* AppContext = nullptr;
    SferaRenderer Renderer;
    SferaInputSystem InputSystem;
    SferaCursorManager CursorManager;
    SferaResourceManager ResourceManager;
    SferaTextureManager TextureManager;
    SferaInterfaceResourceManager InterfaceResources;
    SferaScriptRuntime ScriptRuntime;
    SferaSoundSystem SoundSystem;
    SferaNetworkClient NetworkClient;
    bool bInitialized = false;
};
