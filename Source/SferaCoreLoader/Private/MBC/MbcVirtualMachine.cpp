#include "MBC/MbcVirtualMachine.h"
#include "Core/NumericParse.h"
#include <cmath>
#include <cstring>

namespace Sfera {
FMbcVirtualMachine::FMbcVirtualMachine(FMbcNativeRegistry& registry) : Registry(registry) {}
FStatus FMbcVirtualMachine::LoadProject(const FMbcProject* project) { Project = project; LoadedModule = nullptr; return ValidateLoadedProject(); }
FStatus FMbcVirtualMachine::LoadModule(const FMbcModule* module) { LoadedModule = module; SingleModuleProject = {}; if (module) { SingleModuleProject.Modules().push_back(*module); SingleModuleProject.BuildLinker(); Project = &SingleModuleProject; } return ValidateLoadedModule(); }
FStatus FMbcVirtualMachine::ValidateLoadedProject() const { return Project && !Project->Modules().empty() ? FStatus::Ok() : FStatus::Error(EStatusCode::InvalidData, "no MBC project loaded"); }
FStatus FMbcVirtualMachine::ValidateLoadedModule() const { return LoadedModule && LoadedModule->IsValid() ? FStatus::Ok() : FStatus::Error(EStatusCode::InvalidData, "no MBC module loaded"); }
FStatus FMbcVirtualMachine::RunSymbol(std::string_view symbolName) {
    FStatus status = ValidateLoadedProject();
    if (!status.IsOk()) { return status; }
    const FMbcFunctionSymbol* symbol = Project->Linker().FindInternal(symbolName);
    if (!symbol) { return FStatus::Error(EStatusCode::NotFound, "MBC symbol not found: " + std::string(symbolName)); }
    const FMbcModule& module = Project->Modules()[symbol->ModuleIndex];
    const FMbcProgram* program = symbol->ProgramIndex >= 0 ? module.ProgramForOffset(symbol->CodeOffset) : nullptr;
    if (!program) { return FStatus::Error(EStatusCode::InvalidData, "MBC symbol has no owning program: " + std::string(symbolName)); }
    FMbcExecutionResult result = RunModuleProgram(symbol->ModuleName, program->Name, {});
    return result.Status;
}
FMbcExecutionResult FMbcVirtualMachine::RunModuleProgram(std::string_view moduleName, std::string_view programName, FMbcExecutionLimits limits) {
    FMbcExecutionResult result;
    Events.clear();
    FStatus status = ValidateLoadedProject();
    if (!status.IsOk()) { result.Status = status; return result; }
    const FMbcModule* module = Project->FindModule(moduleName);
    if (!module) { result.Status = FStatus::Error(EStatusCode::NotFound, "MBC module not found"); return result; }
    const FMbcProgram* program = module->ProgramByName(programName);
    if (!program) { result.Status = FStatus::Error(EStatusCode::NotFound, "MBC program not found"); return result; }
    uint32 moduleIndex = 0;
    for (uint32 i = 0; i < Project->Modules().size(); ++i) { if (&Project->Modules()[i] == module) { moduleIndex = i; break; } }
    FMbcRuntimeFrame frame;
    frame.ModuleIndex = moduleIndex;
    frame.ProgramIndex = program->Index;
    frame.Pc = program->Start;
    frame.Data = module->Data();
    frame.Running = true;
    result = RunFrame(frame, limits);
    result.Events = Events;
    return result;
}
FMbcExecutionResult FMbcVirtualMachine::RunFrame(FMbcRuntimeFrame& frame, FMbcExecutionLimits limits) {
    FMbcExecutionResult result;
    const FMbcModule& module = Project->Modules()[frame.ModuleIndex];
    while (frame.Running && frame.Pc < module.Code().size() && result.Instructions < limits.MaxInstructions) {
        uint32 oldPc = frame.Pc;
        FMbcDecodedOpcode decoded = DecodeMbcOpcode(module.Code(), frame.Pc);
        frame.Pc += std::max<uint32>(decoded.Length, 1);
        FStatus status = ExecuteDecoded(frame, decoded);
        ++result.Instructions;
        if (!status.IsOk()) { result.Status = status; result.Events = Events; return result; }
        if (decoded.Terminal && frame.Pc == oldPc + std::max<uint32>(decoded.Length, 1)) { frame.Running = false; }
    }
    result.Halted = !frame.Running;
    if (result.Instructions >= limits.MaxInstructions) { result.Status = FStatus::Error(EStatusCode::RuntimeError, "MBC instruction limit reached"); }
    result.Events = Events;
    return result;
}
FStatus FMbcVirtualMachine::ExecuteDecoded(FMbcRuntimeFrame& frame, const FMbcDecodedOpcode& decoded) {
    const std::string& m = decoded.Mnemonic;
    auto iop = [&](const char* key) -> int32 { auto it = decoded.Operands.find(key); return it == decoded.Operands.end() ? 0 : NumericParse::Int32Or(it->second); };
    auto uop = [&](const char* key) -> uint32 { auto it = decoded.Operands.find(key); return it == decoded.Operands.end() ? 0u : NumericParse::UInt32Or(it->second); };
    if (m == "push_imm32") { uint8 typ = static_cast<uint8>(uop("type")); if (typ == Mbc::TypeFloat) { Push(frame, FMbcValue::Float(Mbc::FloatFromU32(uop("value_u32")))); } else { Push(frame, FMbcValue::Int(iop("value_i32"))); } return FStatus::Ok(); }
    if (m == "push_imm_u16" || m == "push_imm_i8") { Push(frame, FMbcValue::Int(iop("value"))); return FStatus::Ok(); }
    if (m == "push_data_ref") { Push(frame, FMbcValue::Ref(static_cast<uint8>(uop("type")), uop("data_offset"))); return FStatus::Ok(); }
    if (m == "push_inline_span" || m == "push_typed_span_ref" || m == "push_inline_typed_span") { Push(frame, FMbcValue::SliceValue(uop("data_offset"), uop("length"), uop("length"))); return FStatus::Ok(); }
    if (m == "add" || m == "sub" || m == "mul" || m == "div" || m == "mod") { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); if (a.Type == Mbc::TypeFloat || b.Type == Mbc::TypeFloat) { float av = a.Type == Mbc::TypeFloat ? a.FloatValue : static_cast<float>(a.IntValue); float bv = b.Type == Mbc::TypeFloat ? b.FloatValue : static_cast<float>(b.IntValue); if (m == "add") { Push(frame, FMbcValue::Float(av + bv)); } else if (m == "sub") { Push(frame, FMbcValue::Float(av - bv)); } else if (m == "mul") { Push(frame, FMbcValue::Float(av * bv)); } else if (m == "div") { Push(frame, FMbcValue::Float(bv == 0.0f ? 0.0f : av / bv)); } else { Push(frame, FMbcValue::Int(static_cast<int32>(av) % std::max<int32>(1, static_cast<int32>(bv)))); } } else { int32 av = a.IntValue; int32 bv = b.IntValue; if (m == "add") { Push(frame, FMbcValue::Int(av + bv)); } else if (m == "sub") { Push(frame, FMbcValue::Int(av - bv)); } else if (m == "mul") { Push(frame, FMbcValue::Int(av * bv)); } else if (m == "div") { Push(frame, FMbcValue::Int(bv == 0 ? 0 : av / bv)); } else { Push(frame, FMbcValue::Int(bv == 0 ? 0 : av % bv)); } } return FStatus::Ok(); }
    if (m == "eq" || m == "ne" || m == "gt" || m == "lt" || m == "ge" || m == "le") { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); float av = a.Type == Mbc::TypeFloat ? a.FloatValue : static_cast<float>(a.IntValue); float bv = b.Type == Mbc::TypeFloat ? b.FloatValue : static_cast<float>(b.IntValue); bool r = m == "eq" ? av == bv : m == "ne" ? av != bv : m == "gt" ? av > bv : m == "lt" ? av < bv : m == "ge" ? av >= bv : av <= bv; Push(frame, FMbcValue::Int(r ? 1 : 0)); return FStatus::Ok(); }
    if (m == "logical_not") { Push(frame, FMbcValue::Int(Truthy(Pop(frame)) ? 0 : 1)); return FStatus::Ok(); }
    if (m == "neg") { FMbcValue a = Pop(frame); Push(frame, a.Type == Mbc::TypeFloat ? FMbcValue::Float(-a.FloatValue) : FMbcValue::Int(-a.IntValue)); return FStatus::Ok(); }
    if (m == "jmp_rel32" || m == "jmp_rel16") { frame.Pc = uop("target"); return FStatus::Ok(); }
    if (m == "jfalse_rel32" || m == "jfalse_rel16") { bool t = Truthy(Pop(frame)); frame.Pc = t ? uop("fallthrough") : uop("target"); return FStatus::Ok(); }
    if (m == "logical_or_rel16") { bool t = Truthy(Pop(frame)); if (t) { Push(frame, FMbcValue::Int(1)); frame.Pc = uop("target"); } return FStatus::Ok(); }
    if (m == "logical_and_rel16") { bool t = Truthy(Pop(frame)); if (!t) { Push(frame, FMbcValue::Int(0)); frame.Pc = uop("target"); } return FStatus::Ok(); }
    if (m == "call_rel32") { frame.ReturnStack.push_back(uop("return")); frame.Pc = uop("target"); return FStatus::Ok(); }
    if (m == "return" || m == "return_local") { if (frame.ReturnStack.empty()) { frame.Running = false; return FStatus::Ok(); } frame.Pc = frame.ReturnStack.back(); frame.ReturnStack.pop_back(); return FStatus::Ok(); }
    if (m == "halt_interpreter" || m == "end_program" || m == "yield_program") { AddEvent(m == "yield_program" ? "yield" : "halt", m, frame); frame.Running = false; return FStatus::Ok(); }
    if (m == "program_prologue" || m == "set_arg_count" || m == "stack_frame_reset" || m == "push_stack_frame" || m == "pop_stack_frame" || m == "force_int_type" || m == "force_int_type_alt" || m == "force_two_ints") { return FStatus::Ok(); }
    if (m == "builtin_call" || decoded.Builtin) { return ExecuteBuiltin(frame, decoded); }
    if (m == "import_stub_u32" || m == "call_linked_function") { const FMbcFunctionSymbol* sym = Project->Linker().SymbolAt(frame.ModuleIndex, frame.Pc - std::max<uint32>(decoded.Length, 1)); if (sym) { const FMbcNativeImport* nativeSpec = Project->Linker().ResolveNative(sym->Name); if (nativeSpec) { FMbcNativeContext ctx; ctx.Vm = this; ctx.Data = &frame.Data; ctx.Name = sym->Name; if (const FMbcNative* fn = Registry.Find(sym->Name)) { FStatus st = (*fn)(ctx); if (ctx.ReturnValue) { Push(frame, *ctx.ReturnValue); } return st; } if (nativeSpec->PushesValue) { Push(frame, FMbcValue::Int(0)); } AddEvent("native_stub", sym->Name, frame, nativeSpec->Note); return FStatus::Ok(); } const FMbcFunctionSymbol* target = Project->Linker().ResolveImport(*sym); if (target && target->ModuleIndex == frame.ModuleIndex) { frame.ReturnStack.push_back(frame.Pc); frame.Pc = target->CodeOffset; return FStatus::Ok(); } } AddEvent("unresolved_import", m, frame); frame.Running = false; return FStatus::Ok(); }
    AddEvent("unimplemented_opcode", m, frame, decoded.Known ? "known opcode pending exact runtime handler" : "unknown opcode trap"); return FStatus::Ok();
}
FStatus FMbcVirtualMachine::ExecuteBuiltin(FMbcRuntimeFrame& frame, const FMbcDecodedOpcode& decoded) {
    uint32 sub = 0;
    auto it = decoded.Operands.find("subopcode");
    if (it != decoded.Operands.end()) { sub = NumericParse::UInt32Or(it->second); }
    const FMbcBuiltinSpec* builtin = decoded.Builtin ? decoded.Builtin : FindMbcBuiltin(static_cast<uint8>(sub));
    std::string name = builtin ? builtin->Mnemonic : "builtin_unknown";
    switch (sub) {
    case 3: { FMbcValue a = Pop(frame); Push(frame, FMbcValue::Float(std::sin(a.FloatValue))); return FStatus::Ok(); }
    case 4: { FMbcValue a = Pop(frame); Push(frame, FMbcValue::Float(std::cos(a.FloatValue))); return FStatus::Ok(); }
    case 5: { FMbcValue a = Pop(frame); Push(frame, FMbcValue::Float(std::fabs(a.FloatValue))); return FStatus::Ok(); }
    case 6: { FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(std::abs(a.IntValue))); return FStatus::Ok(); }
    case 23: { Push(frame, FMbcValue::Int(static_cast<int32>(frame.Stack.size()))); return FStatus::Ok(); }
    case 25: case 27: { Push(frame, FMbcValue::Int(0)); return FStatus::Ok(); }
    case 37: case 75: case 76: case 137: case 147: { Push(frame, FMbcValue::Int(0)); return FStatus::Ok(); }
    case 45: case 46: { return FStatus::Ok(); }
    case 106: { Push(frame, FMbcValue::Float(0.0f)); return FStatus::Ok(); }
    case 119: { Push(frame, FMbcValue::Int(-1)); return FStatus::Ok(); }
    case 138: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue & b.IntValue)); return FStatus::Ok(); }
    case 139: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue | b.IntValue)); return FStatus::Ok(); }
    case 140: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue ^ b.IntValue)); return FStatus::Ok(); }
    case 141: { FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(~a.IntValue)); return FStatus::Ok(); }
    case 142: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue << (b.IntValue & 31))); return FStatus::Ok(); }
    case 143: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue >> (b.IntValue & 31))); return FStatus::Ok(); }
    case 144: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue & ~(1 << (b.IntValue & 31)))); return FStatus::Ok(); }
    case 145: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int(a.IntValue | (1 << (b.IntValue & 31)))); return FStatus::Ok(); }
    case 146: { FMbcValue b = Pop(frame); FMbcValue a = Pop(frame); Push(frame, FMbcValue::Int((a.IntValue >> (b.IntValue & 31)) & 1)); return FStatus::Ok(); }
    default: { if (const FMbcNative* fn = Registry.Find(name)) { FMbcNativeContext ctx; ctx.Vm = this; ctx.Data = &frame.Data; ctx.Name = name; ctx.Args = frame.Stack; FStatus st = (*fn)(ctx); if (ctx.ReturnValue) { Push(frame, *ctx.ReturnValue); } AddEvent("builtin_bridge", name, frame, ctx.Commentary); return st; } bool pushes = builtin && (std::string(builtin->Semantic).find("push") != std::string::npos || std::string(builtin->Mnemonic).find("get") != std::string::npos || std::string(builtin->Mnemonic).find("query") != std::string::npos); if (pushes) { Push(frame, FMbcValue::Int(0)); } AddEvent("builtin_stub", name, frame, builtin ? builtin->Semantic : "unrecovered builtin"); return FStatus::Ok(); }
    }
}
void FMbcVirtualMachine::Push(FMbcRuntimeFrame& frame, FMbcValue value) { frame.Stack.push_back(value); }
FMbcValue FMbcVirtualMachine::Pop(FMbcRuntimeFrame& frame) { if (frame.Stack.empty()) { return FMbcValue::Int(0); } FMbcValue v = frame.Stack.back(); frame.Stack.pop_back(); return v; }
bool FMbcVirtualMachine::Truthy(const FMbcValue& value) const { if (value.Type == Mbc::TypeFloat) { return value.FloatValue != 0.0f; } if (value.Type == Mbc::TypeSlice) { return value.Slice.Length != 0; } return value.IntValue != 0; }
void FMbcVirtualMachine::AddEvent(std::string kind, std::string name, const FMbcRuntimeFrame& frame, std::string commentary) { FMbcRuntimeEvent e; e.Kind = std::move(kind); e.Name = std::move(name); e.ModuleIndex = frame.ModuleIndex; e.ProgramIndex = frame.ProgramIndex; e.CodeOffset = frame.Pc; e.Commentary = std::move(commentary); Events.push_back(std::move(e)); }
}
