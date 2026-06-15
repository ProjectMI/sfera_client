#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SFERA_CLIENT_CRITICAL_SECTION_COUNT = 3, SFERA_CLIENT_DECODED_GUARD_TOKEN_BYTES = 4 };

typedef int (*SferaClientFatalMessageHandler)(const char* text, char mode);
typedef void (*SferaClientInstallFatalMessageHandler)(SferaClientFatalMessageHandler handler);
typedef void (*SferaClientInstallRawCallback)(void* callback);
typedef int (*SferaClientOpenConfigFile)(const char* fileName);
typedef int (*SferaClientReadIntegerConfigValue)(const char* key, int* value);
typedef int (*SferaClientInitializeSubsystems)(void);
typedef char (*SferaClientInitializeTiming)(long double scale, double step);
typedef void (*SferaClientProcedure)(void);

typedef struct SferaClientMutableBuffer
{
    char* data;
    size_t size;
} SferaClientMutableBuffer;

typedef struct SferaClientMemory
{
    void* primaryInstanceGlobal;
    void* idaCompatibilityInstanceGlobal;
    int* showCommandGlobal;
    SferaClientMutableBuffer commandLine;
    SferaClientMutableBuffer gameXpSession;
    char* connectTypeFlag;
    char* noLoginFlag;
    char** requiredDirectories;
    size_t requiredDirectoryCount;
    void* criticalSections[SFERA_CLIENT_CRITICAL_SECTION_COUNT];
    SferaClientMutableBuffer guardToken;
    uintptr_t guardTokenXorKey;
    const char* guardMessageText;
    const char* guardMessageTitle;
    const char* missingLoginSidText;
    const char* missingLoginSidTitle;
} SferaClientMemory;

typedef struct SferaClientCallbacks
{
    SferaClientFatalMessageHandler fatalMessageHandler;
    SferaClientInstallFatalMessageHandler installFatalMessageHandler;
    void* cleanupCallback;
    SferaClientInstallRawCallback installCleanupCallback;
    SferaClientOpenConfigFile openConfigFile;
    SferaClientReadIntegerConfigValue readIntegerConfigValue;
    SferaClientInitializeSubsystems initializeSubsystems;
    SferaClientInitializeTiming initializeTiming;
    SferaClientProcedure loadConfig;
    SferaClientProcedure loadDebugConfig;
    SferaClientProcedure enterMainLoop;
} SferaClientCallbacks;

typedef struct SferaClientRuntimeConfig
{
    SferaClientMemory memory;
    SferaClientCallbacks callbacks;
} SferaClientRuntimeConfig;

typedef struct SferaClientStartupInfo
{
    void* instance;
    void* previousInstance;
    const char* commandLine;
    int showCommand;
} SferaClientStartupInfo;

int sfera_run_client_application(const SferaClientRuntimeConfig* config, SferaClientStartupInfo startup);

#ifdef __cplusplus
}
#endif
