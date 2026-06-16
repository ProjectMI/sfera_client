#pragma once
#include "SferaBase.h"
#include "SferaResourceTypes.h"

class SferaResourceManager;

struct SferaScriptFileSummary
{
    std::string LogicalPath;
    SferaUInt64 SizeBytes = 0;
    SferaUInt32 Magic = 0;
    SferaUInt32 Word1 = 0;
    SferaUInt32 Word2 = 0;
    bool bReadable = false;
};

class SferaScriptRuntime
{
public:
    bool Initialize(const SferaResourceManager& Resources);
    void Shutdown();
    bool LoadMainProgram();
    void Tick();

    const std::vector<const SferaResourceRecord*>& GetScriptFiles() const { return ScriptFiles; }
    const std::vector<SferaScriptFileSummary>& GetScriptSummaries() const { return ScriptSummaries; }
    bool IsMainProgramPresent() const { return bMainProgramPresent; }

private:
    void BuildScriptSummaries();
    const SferaResourceRecord* FindMainProgramCandidate() const;

private:
    const SferaResourceManager* ResourceManager = nullptr;
    std::vector<const SferaResourceRecord*> ScriptFiles;
    std::vector<SferaScriptFileSummary> ScriptSummaries;
    bool bInitialized = false;
    bool bMainLoaded = false;
    bool bMainProgramPresent = false;
};
