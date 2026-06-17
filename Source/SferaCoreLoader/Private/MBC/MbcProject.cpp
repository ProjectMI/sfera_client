#include "MBC/MbcProject.h"

namespace Sfera {
static std::string StemLikeProject(const std::string& path) { size_t slash = path.find_last_of("/\\"); std::string name = slash == std::string::npos ? path : path.substr(slash + 1); size_t dot = name.find_last_of('.'); return dot == std::string::npos ? name : name.substr(0, dot); }
FStatus FMbcProject::AddModule(std::string name, FByteArray bytes) { FMbcModule module; FStatus status = module.Load(std::move(name), std::move(bytes)); if (!status.IsOk()) { return status; } ModulesLoaded.push_back(std::move(module)); return FStatus::Ok(); }
void FMbcProject::BuildLinker() { ProjectLinker.Build(ModulesLoaded); }
const FMbcModule* FMbcProject::FindModule(std::string_view moduleName) const { std::string wanted(moduleName); for (const auto& module : ModulesLoaded) { if (module.Name() == wanted || StemLikeProject(module.Name()) == wanted) { return &module; } } return nullptr; }
size_t FMbcProject::ProgramCount() const { size_t total = 0; for (const auto& module : ModulesLoaded) { total += module.Programs().size(); } return total; }
size_t FMbcProject::FunctionCount() const { size_t total = 0; for (const auto& module : ModulesLoaded) { total += module.Functions().size(); } return total; }
size_t FMbcProject::ImportCount() const { size_t total = 0; for (const auto& module : ModulesLoaded) { for (const auto& fn : module.Functions()) { if (fn.IsImport()) { ++total; } } } return total; }
}
