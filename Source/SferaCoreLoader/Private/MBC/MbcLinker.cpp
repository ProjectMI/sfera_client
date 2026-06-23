#include "MBC/MbcLinker.h"
#include "Common/StringUtils.h"
#include "MBC/MbcTypes.h"
#include <algorithm>

static bool HasImportStubPayload(const FMbcModule& module, uint32 codeOffset, uint32& payload)
{
    const auto& code = module.Code();

    if (codeOffset + 5 > code.size() || code[codeOffset] != 0x67)
    {
        return false;
    }

    payload = Mbc::ReadU32(code, codeOffset + 1);
    return true;
}

uint64 FMbcProjectLinker::MakeSymbolKey(uint32 moduleIndex, uint32 codeOffset)
{
    return (uint64(moduleIndex) << 32) | uint64(codeOffset);
}

void FMbcProjectLinker::Build(const std::vector<FMbcModule>& modules)
{
    SymbolsFound.clear();
    PatchesFound.clear();
    UnresolvedFound.clear();
    NativeImportsFound.clear();
    ProviderByName.clear();
    NativeByName.clear();
    SymbolByModuleOffset.clear();

    size_t functionCount = 0;
    size_t importCount = 0;
    for (const auto& module : modules)
    {
        functionCount += module.Functions().size();
        for (const auto& fn : module.Functions())
        {
            if (fn.IsImport())
            {
                ++importCount;
            }
        }
    }

    SymbolsFound.reserve(functionCount);
    PatchesFound.reserve(importCount);
    UnresolvedFound.reserve(32);
    NativeImportsFound.reserve(16);
    ProviderByName.reserve(functionCount > importCount ? functionCount - importCount : functionCount);
    SymbolByModuleOffset.reserve(functionCount);
    NativeByName.reserve(16);

    RegisterNative("CreateObj", "engine_import", "engine object factory", true, 2, false);
    RegisterNative("CreateObjWait", "engine_import", "engine object factory wait variant", true, 2, false);
    RegisterNative("DestroyObj", "engine_import", "engine object destroy/release", false);
    RegisterNative("CenterObj", "engine_import", "engine object centering helper", false, 6, false);
    RegisterNative("SetTrig", "engine_import", "engine trigger/state setter", false, 1, false);
    RegisterNative("CountAnim", "engine_import", "animation-count query", true, 0, false);
    RegisterNative("SpeedObj", "engine_import", "object speed query", true, 0, false);
    RegisterNative("DirectOfObj", "engine_import", "object direction query", true, 0, false);
    RegisterNative("InitAI", "engine_import", "AI initialization import", false);
    RegisterNative("InvertAI", "engine_import", "AI state modifier", false);
    RegisterNative("SetRespRadius", "engine_import", "AI/respawn radius setter", false);
    RegisterNative("QueryShowWebShop", "engine_import", "UI/shop native query", true);
    RegisterNative("WaitForAsk", "engine_import", "modal/question wait helper", true);
    RegisterNative("GetPictsPointer", "engine_import", "picture/resource pointer helper", true);
    RegisterNative("PutMoney", "engine_import", "money/transaction UI helper", false);
    RegisterNative("GetMoney", "engine_import", "money/transaction query helper", true);

    for (uint32 moduleIndex = 0; moduleIndex < modules.size(); ++moduleIndex)
    {
        const FMbcModule& module = modules[moduleIndex];
        std::string moduleName = Common::BaseNameWithoutExtension(module.Name());

        for (const auto& fn : module.Functions())
        {
            uint32 payload = 0;
            FMbcFunctionSymbol sym;
            sym.ModuleIndex = moduleIndex;
            sym.FunctionIndex = fn.Index;
            sym.ModuleName = moduleName;
            sym.Name = fn.Name;
            sym.CodeOffset = fn.CodeOffset;
            sym.ProgramIndex = fn.ProgramIndex();
            sym.FlagsOrModule = fn.FlagsOrModule;
            sym.IsImport = fn.IsImport();

            if (sym.IsImport)
            {
                sym.QualifiedName.reserve(sym.ModuleName.size() + 1 + sym.Name.size());
                sym.QualifiedName.append(sym.ModuleName).push_back('.');
                sym.QualifiedName.append(sym.Name);
            }

            if (HasImportStubPayload(module, fn.CodeOffset, payload))
            {
                sym.ImportStubPayload = payload;
            }

            if (!sym.IsImport)
            {
                ProviderByName.try_emplace(sym.Name, SymbolsFound.size());
            }

            SymbolByModuleOffset.emplace(MakeSymbolKey(moduleIndex, fn.CodeOffset), SymbolsFound.size());
            SymbolsFound.push_back(std::move(sym));
        }
    }

    for (const auto& sym : SymbolsFound)
    {
        if (!sym.IsImport) { continue; }

        const FMbcFunctionSymbol* target = FindInternalByName(sym.Name);

        if (target)
        {
            FMbcLinkPatch patch;
            patch.SourceModule = sym.ModuleName;
            patch.SourceName = sym.Name;
            patch.SourceFunctionIndex = sym.FunctionIndex;
            patch.SourceOffset = sym.CodeOffset;
            patch.TargetModule = target->ModuleName;
            patch.TargetName = target->Name;
            patch.TargetFunctionIndex = target->FunctionIndex;
            patch.TargetOffset = target->CodeOffset;
            patch.Rel32 = static_cast<int32>(patch.TargetOffset) - static_cast<int32>(patch.SourceOffset) - 1;
            PatchesFound.push_back(std::move(patch));
            continue;
        }

        if (!ResolveNativeByName(sym.Name))
        {
            UnresolvedFound.push_back(sym);
        }
    }
}

FMbcFunctionSignature FMbcProjectLinker::RecoverSignature(const FMbcModule& module, const FMbcFunction& fn) const
{
    FMbcFunctionSignature sig;
    const auto& code = module.Code();

    if (fn.CodeOffset + 2 > code.size() || code[fn.CodeOffset] != 0x4F)
    {
        return sig;
    }

    int8 signedCount = Mbc::Sign8(code[fn.CodeOffset + 1]);
    sig.Arity = signedCount < 0 ? uint32(-signedCount) : uint32(signedCount);
    sig.Source = "program_prologue";
    return sig;
}

void FMbcProjectLinker::RegisterNative(std::string name, std::string layer, std::string note, bool pushesValue, uint32 arity, bool variadic)
{
    FMbcNativeImport native;
    native.Name = std::move(name);
    native.Layer = std::move(layer);
    native.Note = std::move(note);
    native.PushesValue = pushesValue;
    native.Arity = arity;
    native.Variadic = variadic;
    NativeByName[native.Name] = NativeImportsFound.size();
    NativeImportsFound.push_back(std::move(native));
}

const FMbcFunctionSymbol* FMbcProjectLinker::SymbolAt(uint32 moduleIndex, uint32 codeOffset) const
{
    auto it = SymbolByModuleOffset.find(MakeSymbolKey(moduleIndex, codeOffset));
    return it == SymbolByModuleOffset.end() ? nullptr : &SymbolsFound[it->second];
}

const FMbcFunctionSymbol* FMbcProjectLinker::FindInternalByName(const std::string& name) const
{
    auto it = ProviderByName.find(name);
    return it == ProviderByName.end() ? nullptr : &SymbolsFound[it->second];
}

const FMbcFunctionSymbol* FMbcProjectLinker::FindInternal(std::string_view name) const
{
    auto it = ProviderByName.find(std::string(name));
    return it == ProviderByName.end() ? nullptr : &SymbolsFound[it->second];
}

const FMbcFunctionSymbol* FMbcProjectLinker::ResolveImport(const FMbcFunctionSymbol& importSymbol) const
{
    return FindInternalByName(importSymbol.Name);
}

const FMbcNativeImport* FMbcProjectLinker::ResolveNativeByName(const std::string& name) const
{
    auto it = NativeByName.find(name);
    return it == NativeByName.end() ? nullptr : &NativeImportsFound[it->second];
}

const FMbcNativeImport* FMbcProjectLinker::ResolveNative(std::string_view name) const
{
    auto it = NativeByName.find(std::string(name));
    return it == NativeByName.end() ? nullptr : &NativeImportsFound[it->second];
}
