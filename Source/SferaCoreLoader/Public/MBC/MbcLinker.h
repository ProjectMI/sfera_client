#pragma once
#include "MBC/MbcModule.h"

struct FMbcFunctionSignature
{
    uint32 Arity = 0;
    bool Variadic = false;
    std::vector<uint8> ArgTypes;
    std::string Source = "unknown";
};

struct FMbcFunctionSymbol
{
    uint32 ModuleIndex = 0;
    uint32 FunctionIndex = 0;
    std::string ModuleName;
    std::string Name;
    std::string QualifiedName;
    uint32 CodeOffset = 0;
    int32 ProgramIndex = -1;
    uint32 FlagsOrModule = 0;
    bool IsImport = false;
    std::optional<uint32> ImportStubPayload;
    FMbcFunctionSignature Signature;
};

struct FMbcLinkPatch
{
    std::string SourceModule;
    std::string SourceName;
    uint32 SourceFunctionIndex = 0;
    uint32 SourceOffset = 0;
    std::string TargetModule;
    std::string TargetName;
    uint32 TargetFunctionIndex = 0;
    uint32 TargetOffset = 0;
    int32 Rel32 = 0;
    bool Ambiguous = false;
};

struct FMbcNativeImport
{
    std::string Name;
    std::string Layer;
    std::string Note;
    uint32 Arity = 0;
    bool Variadic = true;
    bool PushesValue = false;
};

class FMbcProjectLinker
{
public:
    void Build(const std::vector<FMbcModule>& modules);
    const std::vector<FMbcFunctionSymbol>& Symbols() const { return SymbolsFound; }
    const std::vector<FMbcLinkPatch>& Patches() const { return PatchesFound; }
    const std::vector<FMbcFunctionSymbol>& UnresolvedImports() const { return UnresolvedFound; }
    const std::vector<FMbcNativeImport>& NativeImports() const { return NativeImportsFound; }
    const FMbcFunctionSymbol* SymbolAt(uint32 moduleIndex, uint32 codeOffset) const;
    const FMbcFunctionSymbol* FindInternal(std::string_view name) const;
    const FMbcFunctionSymbol* ResolveImport(const FMbcFunctionSymbol& importSymbol) const;
    const FMbcNativeImport* ResolveNative(std::string_view name) const;
private:
    FMbcFunctionSignature RecoverSignature(const FMbcModule& module, const FMbcFunction& fn) const;
    void RegisterNative(std::string name, std::string layer, std::string note, bool pushesValue, uint32 arity = 0, bool variadic = true);
    static uint64 MakeSymbolKey(uint32 moduleIndex, uint32 codeOffset);
    const FMbcFunctionSymbol* FindInternalByName(const std::string& name) const;
    const FMbcNativeImport* ResolveNativeByName(const std::string& name) const;
    std::vector<FMbcFunctionSymbol> SymbolsFound;
    std::vector<FMbcLinkPatch> PatchesFound;
    std::vector<FMbcFunctionSymbol> UnresolvedFound;
    std::vector<FMbcNativeImport> NativeImportsFound;
    std::unordered_map<std::string, size_t> ProviderByName;
    std::unordered_map<std::string, size_t> NativeByName;
    std::unordered_map<uint64, size_t> SymbolByModuleOffset;
};
