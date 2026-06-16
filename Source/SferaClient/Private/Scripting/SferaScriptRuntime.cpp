#include "SferaScriptRuntime.h"
#include "SferaResourceManager.h"
#include "SferaBinaryReader.h"

bool SferaScriptRuntime::Initialize(const SferaResourceManager& Resources)
{
    ResourceManager = &Resources;
    ScriptFiles = Resources.FindByKind(ESferaResourceKind::ScriptBytecode);
    std::vector<const SferaResourceRecord*> Libraries = Resources.FindByKind(ESferaResourceKind::ScriptLibrary);
    ScriptFiles.insert(ScriptFiles.end(), Libraries.begin(), Libraries.end());
    bMainProgramPresent = FindMainProgramCandidate() != nullptr;
    BuildScriptSummaries();
    bInitialized = true;
    return true;
}

void SferaScriptRuntime::Shutdown()
{
    ScriptSummaries.clear();
    ScriptFiles.clear();
    ResourceManager = nullptr;
    bMainLoaded = false;
    bMainProgramPresent = false;
    bInitialized = false;
}

const SferaResourceRecord* SferaScriptRuntime::FindMainProgramCandidate() const
{
    if (!ResourceManager)
    {
        return nullptr;
    }

    const std::string Candidates[] =
    {
        "_main.mbc",
        "_main.mbl",
        "mbc\\_main.mbc",
        "mbc\\_main.mbl",
        "__debug.mbc"
    };

    for (const std::string& Candidate : Candidates)
    {
        if (const SferaResourceRecord* Record = ResourceManager->FindResource(Candidate))
        {
            if (Record->bExists)
            {
                return Record;
            }
        }
    }

    return nullptr;
}

void SferaScriptRuntime::BuildScriptSummaries()
{
    ScriptSummaries.clear();
    if (!ResourceManager)
    {
        return;
    }

    for (const SferaResourceRecord* Record : ScriptFiles)
    {
        if (!Record || !Record->bExists)
        {
            continue;
        }

        SferaScriptFileSummary Summary;
        Summary.LogicalPath = Record->LogicalPath;
        Summary.SizeBytes = Record->SizeBytes;

        SferaByteBuffer Bytes;
        if (ResourceManager->ReadBinaryFile(Record->LogicalPath, Bytes) && Bytes.size() >= 4)
        {
            SferaBinaryReader Reader(Bytes);
            Summary.bReadable = true;
            Reader.ReadUInt32LE(Summary.Magic);
            Reader.ReadUInt32LE(Summary.Word1);
            Reader.ReadUInt32LE(Summary.Word2);
        }

        ScriptSummaries.push_back(Summary);
    }
}

bool SferaScriptRuntime::LoadMainProgram()
{
    if (!bInitialized || !ResourceManager)
    {
        return false;
    }

    // Original diagnostics mention: MBC-file: _main, Program: main.
    // S0003 reads fixed-width file headers only. VM dispatch remains a later cluster.
    bMainLoaded = FindMainProgramCandidate() != nullptr;
    return bMainLoaded;
}

void SferaScriptRuntime::Tick()
{
}
