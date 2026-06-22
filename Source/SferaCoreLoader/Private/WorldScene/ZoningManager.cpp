#include "WorldScene/ZoningManager.h"
#include "Common/StringUtils.h"
#include "Core/NumericParse.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <set>

static bool AllAsciiDigits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

static std::vector<float> ParseFloatList(std::string_view value)
{
    std::vector<float> out;
    for (const std::string& token : Common::SplitTokens(value, ",;\t")) { out.push_back(NumericParse::FloatOr(token)); }
    return out;
}
static float ToFloat(std::string_view text, float fallback = 0.0f) { return NumericParse::FloatOr(text, fallback); }
static int32 ToInt(std::string_view text, int32 fallback = 0) { return NumericParse::Int32Or(text, fallback); }
static std::string QualifiedKey(const FConfigEntry& entry) { return Common::ToLower(entry.Scope.empty() ? entry.Key : entry.Scope + "." + entry.Key); }
FStatus FZoningManager::LoadDefault(const FResourceManager& resources, FLogger* logger)
{
    LoadedDocuments.clear();
    FlattenedZones.clear();

    if (!LoadOne(resources, "Landscape/zoning.cfg", logger).IsOk())
    {
        LoadOne(resources, "zoning.cfg", logger);
    }

    if (!LoadOne(resources, "Landscape/zoningHaron.cfg", logger).IsOk())
    {
        LoadOne(resources, "zoningharon.cfg", logger);
    }

    if (logger)
    {
        logger->Info("WorldScene zoning loaded: documents=" + std::to_string(LoadedDocuments.size()) + ", zones=" + std::to_string(FlattenedZones.size()));
    }

    return FStatus::Ok();
}

FStatus FZoningManager::LoadOne(const FResourceManager& resources, std::string_view logicalName, FLogger* logger)
{
    auto blob = resources.Load(logicalName);

    if (!blob.IsOk())
    {
        if (logger)
        {
            logger->Warning("WorldScene zoning missing: " + std::string(logicalName));
        }

        return blob.Status();
    }

    std::string text(blob.Value().Bytes.begin(), blob.Value().Bytes.end());
    FConfigDocument cfg;
    FStatus status = cfg.Parse(std::move(text), std::string(logicalName));

    if (!status.IsOk()) { return status; }

    FZoningDocument doc = BuildDocument(cfg);

    for (const auto& zone : doc.Zones)
    {
        FlattenedZones.push_back(zone);
    }

    if (logger)
    {
        logger->Info("WorldScene zoning parsed: " + std::string(logicalName) + ", entries=" + std::to_string(doc.RawEntryCount) + ", scopes="
        + std::to_string(doc.ScopeCount) + ", zone_fields=" + std::to_string(doc.ZoneFieldCount)
        + ", zones=" + std::to_string(doc.Zones.size()) + ", container_decls="
        + std::to_string(doc.ContainerDeclarationCount) + ", warnings=" + std::to_string(doc.Warnings.size()));

        for (size_t i = 0; i < doc.ScopeSamples.size() && i < 4; ++i)
        {
            logger->Info("WorldScene zoning scope[" + std::to_string(i) + "]: " + doc.ScopeSamples[i]);
        }

        for (size_t i = 0; i < doc.Notes.size() && i < 4; ++i)
        {
            logger->Info("WorldScene zoning note[" + std::to_string(i) + "]: " + doc.Notes[i]);
        }

        if (doc.Notes.size() > 4)
        {
            logger->Info("WorldScene zoning notes suppressed: " + std::to_string(doc.Notes.size() - 4) + " additional notes");
        }

        for (size_t i = 0; i < doc.Warnings.size() && i < 8; ++i)
        {
            logger->Warning("WorldScene zoning warning[" + std::to_string(i) + "]: " + doc.Warnings[i]);
        }

        if (doc.Warnings.size() > 8)
        {
            logger->Warning("WorldScene zoning warnings suppressed: " + std::to_string(doc.Warnings.size() - 8) + " additional warnings");
        }

        for (size_t i = 0; i < doc.Zones.size() && i < 2; ++i)
        {
            const auto& z = doc.Zones[i];
            logger->Info("WorldScene zoning zone[" + std::to_string(i) + "]: index="
            + std::to_string(z.Index) + ", scope=" + z.SourceScope + ", fields="
            + std::to_string(z.RawFields.size()) + ", bounds="
            + std::string(z.Bounds.IsValid() ? "valid" : "unknown"));
        }
    }

    LoadedDocuments.push_back(std::move(doc));
    return FStatus::Ok();
}

const FWorldZoneParams* FZoningManager::FindZone(FVector3 position) const
{
    for (const auto& zone : FlattenedZones)
    {
        if (zone.Bounds.IsValid()
        && position.X >= zone.Bounds.Min.X && position.X <= zone.Bounds.Max.X
        && position.Z >= zone.Bounds.Min.Z && position.Z <= zone.Bounds.Max.Z
        && position.Y >= zone.Bounds.Min.Y && position.Y <= zone.Bounds.Max.Y)
        {
            return &zone;
        }
    }

    return nullptr;
}

FZoningDocument FZoningManager::BuildDocument(const FConfigDocument& cfg)
{
    FZoningDocument doc;
    doc.Name = cfg.SourceName();
    doc.RawEntryCount = cfg.Entries().size();
    std::unordered_map<uint32, FWorldZoneParams> zones;
    std::set<std::string> scopes;
    std::set<std::string> seenUnscopedKeys;
    uint32 currentZone = 0;
    bool haveSequentialZone = false;

    for (const auto& entry : cfg.Entries())
    {
        if (!entry.Scope.empty())
        {
            scopes.insert(entry.Scope);
        }

        std::string q = QualifiedKey(entry);
        std::string keyLower = Common::ToLower(entry.Key);
        bool beginsZoneBlock = keyLower == "zonesparams" || keyLower == "zone" || keyLower == "zones" || keyLower == "zoneparams";

        if (beginsZoneBlock)
        {
            std::string v = Common::Trim(entry.Value);

            if (AllAsciiDigits(v))
            {
                currentZone = NumericParse::UInt32Or(v);
                haveSequentialZone = true;
                zones[currentZone].Index = currentZone;
                zones[currentZone].SourceScope = entry.Scope.empty() ? entry.Key : entry.Scope;
                zones[currentZone].Name = "zone_" + std::to_string(currentZone);
                continue;
            }
        }

        auto extracted = ExtractZoneIndex(entry);

        if (!extracted && entry.Scope.empty() && !haveSequentialZone)
        {
            std::string kseq = keyLower;
            bool starter = kseq == "xpatchmin" || kseq == "xmin" || kseq == "min" || kseq == "name" || kseq == "zonesparams";

            if (starter && seenUnscopedKeys.find(kseq) != seenUnscopedKeys.end())
            {
                ++currentZone;
            }

            seenUnscopedKeys.insert(kseq);
        }

        uint32 index = extracted ? *extracted : (haveSequentialZone ? currentZone : currentZone);
        bool likelyZoneField = extracted.has_value()
        || q.find("zonesparams") != std::string::npos || q.find("zone") != std::string::npos
        || q.find("xmin") != std::string::npos || q.find("xmax") != std::string::npos
        || q.find("zmin") != std::string::npos || q.find("zmax") != std::string::npos
        || q.find("patch") != std::string::npos || q.find("fog") != std::string::npos
        || q.find("ambient") != std::string::npos || q.find("sun") != std::string::npos;

        if (!likelyZoneField) { continue; }

        auto& zone = zones[index];
        zone.Index = index;

        if (zone.SourceScope.empty())
        {
            zone.SourceScope = entry.Scope;
        }

        if (IsContainerDeclaration(entry))
        {
            ++doc.ContainerDeclarationCount;

            if (doc.Notes.size() < 16)
            {
                doc.Notes.push_back(DescribeEntry(entry, "container declaration; opens array/scope and carries no runtime value"));
            }

            continue;
        }

        zone.RawFields[q] = entry.Value;
        ++doc.ZoneFieldCount;

        if (!SetKnownField(zone, entry))
        {
            doc.Warnings.push_back(DescribeEntry(entry, "unmapped zoning key"));
        }
    }

    if (zones.empty() && !cfg.Entries().empty())
    {
        FWorldZoneParams zone;
        zone.Index = 0;
        zone.SourceScope = "implicit";

        for (const auto& entry : cfg.Entries())
        {
            if (IsContainerDeclaration(entry))
            {
                ++doc.ContainerDeclarationCount;

                if (doc.Notes.size() < 16)
                {
                    doc.Notes.push_back(DescribeEntry(entry, "container declaration; opens array/scope and carries no runtime value"));
                }

                continue;
            }

            zone.RawFields[QualifiedKey(entry)] = entry.Value;
            SetKnownField(zone, entry);
        }

        zones[0] = zone;
    }

    doc.ScopeCount = scopes.size();

    for (const auto& scope : scopes)
    {
        if (doc.ScopeSamples.size() < 8)
        {
            doc.ScopeSamples.push_back(scope);
        }
    }

    for (auto& pair : zones)
    {
        if (pair.second.Name.empty())
        {
            pair.second.Name = "zone_" + std::to_string(pair.second.Index);
        }

        doc.Zones.push_back(pair.second);
    }

    std::sort(doc.Zones.begin(), doc.Zones.end(), [](const FWorldZoneParams& a, const FWorldZoneParams& b)
    {
        return a.Index < b.Index;
    });

    if (doc.Zones.empty())
    {
        doc.Warnings.push_back("zonesParams is empty or not recognized");
    }

    return doc;
}

std::optional<uint32> FZoningManager::ExtractZoneIndex(const FConfigEntry& entry)
{
    std::string q = QualifiedKey(entry);
    size_t pos = q.find("zonesparams");

    if (pos == std::string::npos)
    {
        pos = q.find("zone");
    }

    if (pos == std::string::npos)
    {
        return std::nullopt;
    }

    size_t bracket = q.find('[', pos);

    if (bracket != std::string::npos)
    {
        size_t end = q.find(']', bracket);

        if (end != std::string::npos)
        {
            std::string n = q.substr(bracket + 1, end - bracket - 1);

            if (AllAsciiDigits(n))
            {
                return NumericParse::UInt32Or(n);
            }
        }
    }

    size_t dot = q.find('.', pos);

    while (dot != std::string::npos)
    {
        size_t n = dot + 1;

        if (n < q.size() && std::isdigit(static_cast<unsigned char>(q[n])))
        {
            size_t e = n;

            while (e < q.size() && std::isdigit(static_cast<unsigned char>(q[e])))
            {
                ++e;
            }

            return NumericParse::UInt32Or(q.substr(n, e - n));
        }

        dot = q.find('.', dot + 1);
    }

    return std::nullopt;
}

bool FZoningManager::IsContainerDeclaration(const FConfigEntry& entry)
{
    std::string key = Common::ToLower(entry.Key);
    bool arrayTag = !entry.TypeTag.empty() && entry.TypeTag.find('a') != std::string::npos;

    if (!arrayTag) { return false; }

    if (!Common::Trim(entry.Value).empty()) { return false; }

    return key == "zonesparams" || key == "daycolors";
}

std::string FZoningManager::DescribeEntry(const FConfigEntry& entry, std::string_view reason)
{
    std::string value = Common::Trim(entry.Value);
    std::string raw = Common::Trim(entry.RawLine);

    if (value.size() > 80)
    {
        value = value.substr(0, 77) + "...";
    }

    if (raw.size() > 120)
    {
        raw = raw.substr(0, 117) + "...";
    }

    std::string out = std::string(reason) + ": line=" + std::to_string(entry.Line) + ", scope=" + (entry.Scope.empty() ? "<root>" : entry.Scope) + ", key=" + entry.Key;

    if (!entry.TypeTag.empty())
    {
        out += ", type=<" + entry.TypeTag + ">";
    }

    out += ", value=" + (value.empty() ? "<empty>" : value);

    if (!raw.empty())
    {
        out += ", raw=\"" + raw + "\"";
    }

    return out;
}

bool FZoningManager::SetKnownField(FWorldZoneParams& zone, const FConfigEntry& entry)
{
    std::string k = Common::ToLower(entry.Key);
    std::string q = QualifiedKey(entry);
    std::vector<float> nums = ParseFloatList(entry.Value);
    auto setVec3 = [&](FVector3& dst) -> bool
    {
        if (nums.size() >= 3)
        {
            dst =
            {
                nums[0], nums[1], nums[2]
            };
            return true;
        }

        return false;
    };

    auto setColor = [&](FColor3& dst) -> bool
    {
        if (nums.size() >= 3)
        {
            dst =
            {
                nums[0], nums[1], nums[2]
            };
            return true;
        }

        return false;
    };

    if (k == "name") { zone.Name = entry.Value; return true; }

    if (k == "min" || k == "mins" || k == "boundmin") { return setVec3(zone.Bounds.Min); }

    if (k == "max" || k == "maxs" || k == "boundmax") { return setVec3(zone.Bounds.Max); }

    if (k == "xmin") { zone.Bounds.Min.X = ToFloat(entry.Value); return true; }

    if (k == "xmax") { zone.Bounds.Max.X = ToFloat(entry.Value); return true; }

    if (k == "zmin") { zone.Bounds.Min.Z = ToFloat(entry.Value); return true; }

    if (k == "zmax") { zone.Bounds.Max.Z = ToFloat(entry.Value); return true; }

    if (k == "ymin") { zone.Bounds.Min.Y = ToFloat(entry.Value); return true; }

    if (k == "ymax") { zone.Bounds.Max.Y = ToFloat(entry.Value); return true; }

    if (k == "xpatchmin") { zone.PatchMinX = ToInt(entry.Value); return true; }

    if (k == "zpatchmin") { zone.PatchMinZ = ToInt(entry.Value); return true; }

    if (k == "borderfadedist") { zone.BorderFadeDist = ToFloat(entry.Value); return true; }

    if (k == "skyfogalpha") { zone.Lighting.SkyFogAlpha = ToFloat(entry.Value); return true; }

    if (k == "fognear") { zone.Lighting.FogNear = ToFloat(entry.Value); return true; }

    if (k == "fogfar") { zone.Lighting.FogFar = ToFloat(entry.Value); return true; }

    auto colorSet = [&](std::array<FColor3, 8>& colors, const std::string& prefix) -> bool
    {
        size_t p = q.find(prefix);

        if (p == std::string::npos) { return false; }

        size_t b = q.find('[', p);
        uint32 idx = 0;

        if (b != std::string::npos)
        {
            size_t e = q.find(']', b);

            if (e != std::string::npos)
            {
                std::string n = q.substr(b + 1, e - b - 1);

                if (AllAsciiDigits(n))
                {
                    idx = NumericParse::UInt32Or(n);
                }
            }
        }

        if (idx >= colors.size())
        {
            idx = 0;
        }

        if (setColor(colors[idx])) { return true; }

        if (q.rfind(".r") != std::string::npos || q.find("r]") != std::string::npos)
        {
            colors[idx].R = ToFloat(entry.Value);
            return true;
        }

        if (q.rfind(".g") != std::string::npos || q.find("g]") != std::string::npos)
        {
            colors[idx].G = ToFloat(entry.Value);
            return true;
        }

        if (q.rfind(".b") != std::string::npos || q.find("b]") != std::string::npos)
        {
            colors[idx].B = ToFloat(entry.Value);
            return true;
        }

        return false;
    };

    if (colorSet(zone.Lighting.FogColor, "fogcolor")) { return true; }

    if (colorSet(zone.Lighting.AmbientColor, "ambientcolor")) { return true; }

    if (colorSet(zone.Lighting.SunColor, "suncolor")) { return true; }

    return false;
}
