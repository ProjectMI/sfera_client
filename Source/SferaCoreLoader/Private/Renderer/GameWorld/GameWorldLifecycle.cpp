#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

namespace
{
struct FWeatherIniSection
{
    std::string Name;
    std::unordered_map<std::string, std::string> Values;
};

struct FWeatherTextureDefinition
{
    std::string Texture;
    float CloudCover = 0.25f;
    float ScrollScale = 1.0f;
};

std::string TrimWeatherText(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last)
    {
        return {};
    }
    value = std::string(first, last);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
    {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string NormalizeWeatherText(std::string value)
{
    value = TrimWeatherText(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ParseWeatherFloat(const std::string& text, float& value)
{
    const std::string clean = TrimWeatherText(text);
    if (clean.empty())
    {
        return false;
    }
    char* end = nullptr;
    value = std::strtof(clean.c_str(), &end);
    return end != clean.c_str();
}

bool ParseWeatherInt(const std::string& text, int& value)
{
    const std::string clean = TrimWeatherText(text);
    if (clean.empty())
    {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(clean.c_str(), &end, 10);
    if (end == clean.c_str())
    {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

const std::string* WeatherValue(const FWeatherIniSection& section, std::string_view key)
{
    const auto it = section.Values.find(std::string(key));
    return it == section.Values.end() ? nullptr : &it->second;
}

bool IsIndexedWeatherSection(const std::string& name, char prefix, std::size_t digits)
{
    if (name.size() != digits + 1 || name.front() != prefix)
    {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool ParseWeatherScenarioKeyframeSection(const std::string& name, int& scenarioIndex)
{
    if (name.size() != 5 || name[0] != 's' || name[3] != 't' || !std::isdigit(static_cast<unsigned char>(name[1])) || !std::isdigit(static_cast<unsigned char>(name[2])) || !std::isdigit(static_cast<unsigned char>(name[4])))
    {
        return false;
    }
    scenarioIndex = (name[1] - '0') * 10 + name[2] - '0';
    return true;
}

std::vector<FWeatherIniSection> ParseWeatherSections(const std::vector<std::string>& lines)
{
    std::vector<FWeatherIniSection> sections;
    FWeatherIniSection* current = nullptr;
    for (std::string line : lines)
    {
        const std::size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos)
        {
            line.resize(comment);
        }
        line = TrimWeatherText(std::move(line));
        if (line.empty())
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            sections.push_back(FWeatherIniSection{});
            current = &sections.back();
            current->Name = NormalizeWeatherText(line.substr(1, line.size() - 2));
            continue;
        }
        std::size_t split = line.find('=');
        if (split == std::string::npos)
        {
            split = line.find(':');
        }
        if (split == std::string::npos)
        {
            continue;
        }
        if (!current)
        {
            sections.push_back(FWeatherIniSection{});
            current = &sections.back();
            current->Name = "global";
        }
        const std::string key = NormalizeWeatherText(line.substr(0, split));
        if (!key.empty())
        {
            current->Values[key] = TrimWeatherText(line.substr(split + 1));
        }
    }
    return sections;
}

float NormalizeWeatherLevel(float value)
{
    value = std::abs(value);
    if (value > 1.5f)
    {
        value *= value <= 100.0f ? 0.01f : 0.001f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

float NormalizeWeatherDuration(float value)
{
    value = std::abs(value);
    if (value > 10000.0f)
    {
        value *= 0.001f;
    }
    else if (value > 1800.0f)
    {
        value *= 0.1f;
    }
    return std::clamp(value > 0.0f ? value : 120.0f, 30.0f, 900.0f);
}

}

static bool IsTerrainLndPath(const std::filesystem::path& Path)
{
    return Common::NormalizePathKey(Path.extension().string()) == ".lnd";
}

static std::string TerrainStemKey(const std::string& TerrainStem)
{
    return Common::NormalizePathKey(Common::StripExtension(TerrainStem));
}

FD3D9GameWorldScene::Impl::~Impl()
{
    Release();
}

void FD3D9GameWorldScene::Impl::Release()
{
    StopGrassMapPreloadWorker();
    DrainTerrainCpuPreloadJobs(true);
    DrainStaticModelCpuPreloadJobs(true);
    PendingTerrainCpuPreloads.clear();
    CompletedTerrainCpuPreloads.clear();
    PendingTerrainGpuPromotions.clear();
    PendingStaticModelCpuPreloads.clear();
    CompletedStaticModelCpuPreloads.clear();
    QueuedTerrainCpuPreloads.clear();
    QueuedTerrainGpuPromotions.clear();
    QueuedStaticModelCpuPreloads.clear();
    QueuedTerrainCpuPreloadCells.clear();

    SafeRelease(OverlayTexture);
    SafeRelease(TerrainMicrotexture);
    SafeRelease(SkyTexture);
    SafeRelease(WeatherSkyCurrent1);
    SafeRelease(WeatherSkyCurrent2);
    SafeRelease(WeatherSkyNext1);
    SafeRelease(WeatherSkyNext2);
    SafeRelease(RainTexture);
    SafeRelease(WaterTexture);
    ReflectionTextureReady = false;
    ReflectionWarmupFrames = 0;
    SafeRelease(ReflectionDepth);
    SafeRelease(ReflectionSurface);
    SafeRelease(ReflectionTexture);
    SafeRelease(BaseVS);
    SafeRelease(BasePS);
    SafeRelease(GrassVS);
    SafeRelease(GrassPS);
    SafeRelease(WorldDecl);
    SafeRelease(AnimatedWorldDecl);
    WorldShadersReady = false;
    BaseVSConsts.clear();
    BaseVsWorldViewProjection = -1;
    BaseVsDirLightToLightDirL = -1;
    BaseVsDirLightColor = -1;
    BaseVsAmbientColor = -1;
    ViewFrustumReady = false;
    for (auto& batch : PlayerBatches)
    {
        SafeRelease(batch.Texture);
    }
    PlayerBatches.clear();
    ClearRemotePlayers();
    ClearRemoteActors();
    MonsterModelNames.clear();
    MonsterModelIndexReady = false;
    SafeRelease(PlayerIndexBuffer);
    SafeRelease(PlayerVertexBuffer);
    PlayerPoseCache = {};
    PlayerSkinScratch.clear();
    PlayerVertexScratch.clear();
    PlayerModel = {};
    for (auto& [_, resource] : TerrainResources)
    {
        SafeRelease(resource->texture);
        SafeRelease(resource->IndexBuffer);
        SafeRelease(resource->VertexBuffer);
        SafeRelease(resource->WaterIndexBuffer);
        SafeRelease(resource->WaterVertexBuffer);
    }
    ClearGrassRenderBatches();
    StopGrassRenderBakeWorker();
    GrassInstancesByCell.clear();
    GrassInstanceCount = 0;
    GrassCells.clear();
    GrassTargetCells.clear();
    GrassPendingCells.clear();
    GrassDrawBatches.clear();
    GrassDrawBatchesDirty = true;
    DrainStaticRenderCellBakeJobs(true);
    StopStaticRenderBakeWorker();
    for (auto& promotion : PendingStaticGpuPromotions)
    {
        if (!promotion.Resource)
        {
            continue;
        }
        for (auto& batch : promotion.Resource->Batches)
        {
            SafeRelease(batch.Texture);
        }
        SafeRelease(promotion.Resource->IndexBuffer);
        SafeRelease(promotion.Resource->StaticAttributeVertexBuffer);
        SafeRelease(promotion.Resource->AnimatedVertexBuffer);
        SafeRelease(promotion.Resource->VertexBuffer);
    }
    PendingStaticGpuPromotions.clear();
    QueuedStaticGpuPromotions.clear();
    for (auto& [_, resource] : StaticResources)
    {
        for (auto& batch : resource->Batches)
        {
            SafeRelease(batch.Texture);
        }
        SafeRelease(resource->IndexBuffer);
        SafeRelease(resource->StaticAttributeVertexBuffer);
        SafeRelease(resource->AnimatedVertexBuffer);
        SafeRelease(resource->VertexBuffer);
    }
    ClearStaticRenderBatches();
    for (auto& [_, texture] : DdsTextureCache)
    {
        SafeRelease(texture);
    }
    DdsTextureCache.clear();
    for (auto& Resources : GrassPatternResources) { Resources.clear(); }
    for (auto& Resources : GrassFlowerPatternResources) { Resources.clear(); }
    GrassDetailResources.clear();
    TerrainInitialBlockingLoad = false;
    StaticInitialBlockingLoad = false;
    StaticRefreshPending = false;
    GrassRefreshIncomplete = false;
    GrassInitialBlockingLoad = false;
    {
        std::lock_guard<std::mutex> lock(GrassMapMutex);
        GrassMaps.clear();
    }
    WeatherScenarios.clear();
    WeatherSequence.clear();
    WeatherSequencePosition = 0;
    WeatherScenarioElapsed = 0.0f;
    WeatherTransitionBlend = 0.0f;
    StaticInstances.clear();
    StaticCollisionInstances.clear();
    StaticCollisionCells.clear();
    LargeStaticCollisionInstances.clear();
    StaticCollisionVisitMarks.clear();
    StaticCollisionInstanceScratch.clear();
    StaticCollisionTriangleScratch.clear();
    StaticCollisionVisitGeneration = 0;
    StaticPlacementModels.clear();
    StaticPlacements.clear();
    StaticPlacementIndicesByRenderCell.clear();
    VisibleStaticPlacementIndices.clear();
    VisibleStaticRenderCells.clear();
    StaticDrawBatches.clear();
    StaticDrawBatchesDirty = true;
    VisibleAnimatedResources.clear();
    StaticVisibilityPlanReady = false;
    StaticResources.clear();
    TerrainInstances.clear();
    TerrainInstanceLookup.clear();
    TerrainResources.clear();
    StreamingGuardRow = (std::numeric_limits<int>::min)();
    StreamingGuardColumn = (std::numeric_limits<int>::min)();
    StreamingGuardRowStep = (std::numeric_limits<int>::min)();
    StreamingGuardColumnStep = (std::numeric_limits<int>::min)();
    QueuedTerrainCpuPreloadCells.clear();
    {
        std::lock_guard<std::mutex> lock(OptionalPathCacheMutex);
        OptionalPathCache.clear();
    }
    TerrainLndPathByRelativeKey.clear();
    TerrainLndPathByStemKey.clear();
    TerrainPathIndexReady = false;
    TerrainStemPathCache.clear();
    TerrainRelativePathCache.clear();
    ModelPathCache.clear();
    ModelPathIndex.clear();
    ModelPathIndexReady = false;
    ModelTexturePathCache.clear();
    SafeRelease(Device);
    Initialized = false;
}


std::filesystem::path FD3D9GameWorldScene::Impl::ResolveOptionalPath(std::string LogicalName) const
{
    if (!AssetResources || LogicalName.empty())
    {
        return {};
    }
    const std::string key = Common::NormalizePathKey(LogicalName);
    {
        std::lock_guard<std::mutex> lock(OptionalPathCacheMutex);
        if (const auto cached = OptionalPathCache.find(key); cached != OptionalPathCache.end())
        {
            return cached->second;
        }
    }
    std::filesystem::path resolved;
    auto direct = AssetResources->Catalog().FindByLogicalName(LogicalName);
    if (direct)
    {
        resolved = direct->AbsolutePath;
    }
    else
    {
        for (const auto& record : AssetResources->Catalog().All())
        {
            const std::string rel = Common::NormalizePathKey(record.RelativePath);
            if (rel == key || rel.ends_with("/" + key))
            {
                resolved = record.AbsolutePath;
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(OptionalPathCacheMutex);
        return OptionalPathCache.emplace(key, resolved).first->second;
    }
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveConfiguredPath(const std::string& LogicalName) const
{
    const auto path = ResolveOptionalPath(LogicalName);
    if (!path.empty())
    {
        return path;
    }
    throw std::runtime_error("required configured asset is missing: " + LogicalName);
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveWeatherTexturePath(const std::string& TextureName) const
{
    if (!AssetResources)
    {
        return {};
    }
    std::string name = TrimWeatherText(TextureName);
    std::replace(name.begin(), name.end(), '\\', '/');
    const std::string normalized = NormalizeWeatherText(name);
    if (normalized.empty() || normalized == "none")
    {
        return {};
    }
    std::vector<std::string> candidates;
    candidates.reserve(8);
    candidates.push_back(name);
    const std::filesystem::path sourcePath(name);
    if (sourcePath.extension().empty())
    {
        candidates.push_back(name + ".dds");
        candidates.push_back("landscape/" + name + ".dds");
        candidates.push_back("landscape_hr/" + name + ".dds");
        candidates.push_back("textures/" + name + ".dds");
        candidates.push_back("effects/" + name + ".dds");
    }
    for (const std::string& candidate : candidates)
    {
        const auto resolved = ResolveOptionalPath(candidate);
        if (!resolved.empty() && Common::NormalizePathKey(resolved.extension().string()) == ".dds")
        {
            return resolved;
        }
    }
    const std::string requestedStem = Common::NormalizePathKey(sourcePath.stem().string());
    std::filesystem::path fallback;
    for (const auto& record : AssetResources->Catalog().All())
    {
        if (Common::NormalizePathKey(record.RelativePath.extension().string()) != ".dds")
        {
            continue;
        }
        if (Common::NormalizePathKey(record.RelativePath.stem().string()) != requestedStem)
        {
            continue;
        }
        const std::string relative = Common::NormalizePathKey(record.RelativePath);
        if (relative.find("landscape/") != std::string::npos || relative.find("effects/") != std::string::npos)
        {
            return record.AbsolutePath;
        }
        if (fallback.empty())
        {
            fallback = record.AbsolutePath;
        }
    }
    return fallback;
}

void FD3D9GameWorldScene::Impl::RefreshWeatherSkyTextures()
{
    SafeRelease(WeatherSkyCurrent1);
    SafeRelease(WeatherSkyCurrent2);
    SafeRelease(WeatherSkyNext1);
    SafeRelease(WeatherSkyNext2);
    if (WeatherScenarios.empty() || WeatherSequence.empty())
    {
        return;
    }
    auto load = [&](const std::string& name) -> IDirect3DTexture9*
    {
        const auto path = ResolveWeatherTexturePath(name);
        if (path.empty())
        {
            return nullptr;
        }
        try
        {
            return LoadCachedDdsTexture(path);
        }
        catch (const std::exception& exception)
        {
            if (Logger)
            {
                Logger->Warning("Weather texture load failed: " + path.string() + ": " + exception.what());
            }
            return nullptr;
        }
    };
    const std::size_t currentSequence = WeatherSequencePosition % WeatherSequence.size();
    const std::size_t nextSequence = (currentSequence + 1) % WeatherSequence.size();
    const auto& current = WeatherScenarios[WeatherSequence[currentSequence]];
    const auto& next = WeatherScenarios[WeatherSequence[nextSequence]];
    WeatherSkyCurrent1 = load(current.Sky1);
    WeatherSkyCurrent2 = load(current.Sky2);
    WeatherSkyNext1 = load(next.Sky1);
    WeatherSkyNext2 = load(next.Sky2);
}

void FD3D9GameWorldScene::Impl::LoadWeatherSystem()
{
    WeatherScenarios.clear();
    WeatherSequence.clear();
    WeatherSequencePosition = 0;
    WeatherScenarioElapsed = 0.0f;
    WeatherTransitionBlend = 0.0f;
    WeatherRain = 0.0f;
    WeatherCloudCover = 0.0f;
    WeatherFog = 0.0f;
    WeatherWind = 0.35f;
    WeatherSkyScrollScale = 1.0f;
    SafeRelease(RainTexture);
    if (!Config.WeatherEnabled || !WorldScene)
    {
        ApplyWeatherEnvironment();
        return;
    }

    const auto sections = ParseWeatherSections(WorldScene->Weather().Lines);
    std::unordered_map<std::string, FWeatherTextureDefinition> textureDefinitions;
    for (const auto& section : sections)
    {
        const std::string* textureValue = WeatherValue(section, "ntex");
        if (!textureValue)
        {
            continue;
        }
        FWeatherTextureDefinition definition;
        definition.Texture = TrimWeatherText(*textureValue);
        if (const std::string* scrollValue = WeatherValue(section, "scrll"))
        {
            float scroll = 0.0f;
            if (ParseWeatherFloat(*scrollValue, scroll))
            {
                if (std::abs(scroll) > 10.0f)
                {
                    scroll *= 0.01f;
                }
                definition.ScrollScale = std::clamp(std::abs(scroll), 0.0f, 4.0f);
            }
        }
        float channelSum = 0.0f;
        int channelCount = 0;
        for (const auto& [key, value] : section.Values)
        {
            if (key.size() < 2 || (key.front() != 'r' && key.front() != 'g' && key.front() != 'b'))
            {
                continue;
            }
            if (!std::all_of(key.begin() + 1, key.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            {
                continue;
            }
            float channel = 0.0f;
            if (ParseWeatherFloat(value, channel))
            {
                channelSum += channel <= 1.5f ? channel * 255.0f : channel;
                ++channelCount;
            }
        }
        if (channelCount > 0)
        {
            const float brightness = channelSum / static_cast<float>(channelCount);
            definition.CloudCover = std::clamp((190.0f - brightness) / 135.0f, 0.05f, 1.0f);
        }
        textureDefinitions[section.Name] = definition;
        textureDefinitions[NormalizeWeatherText(definition.Texture)] = definition;
    }

    std::array<std::size_t, 100> scenarioBySourceIndex{};
    scenarioBySourceIndex.fill((std::numeric_limits<std::size_t>::max)());
    for (const auto& section : sections)
    {
        if (!IsIndexedWeatherSection(section.Name, 's', 2) || !WeatherValue(section, "stime"))
        {
            continue;
        }
        const int sourceIndex = (section.Name[1] - '0') * 10 + section.Name[2] - '0';
        WeatherScenario scenario;
        scenario.Name = section.Name;
        float duration = 120.0f;
        ParseWeatherFloat(*WeatherValue(section, "stime"), duration);
        scenario.Duration = NormalizeWeatherDuration(duration);
        if (const std::string* value = WeatherValue(section, "nsky1"))
        {
            scenario.Sky1 = TrimWeatherText(*value);
        }
        if (const std::string* value = WeatherValue(section, "nsky2"))
        {
            scenario.Sky2 = TrimWeatherText(*value);
        }
        float scrollScaleSum = 0.0f;
        int scrollScaleCount = 0;
        auto applyTextureDefinition = [&](std::string& skyName)
        {
            const std::string key = NormalizeWeatherText(skyName);
            if (key.empty() || key == "none")
            {
                skyName.clear();
                return;
            }
            const auto it = textureDefinitions.find(key);
            if (it != textureDefinitions.end())
            {
                skyName = it->second.Texture;
                scenario.CloudCover = (std::max)(scenario.CloudCover, it->second.CloudCover);
                scrollScaleSum += it->second.ScrollScale;
                ++scrollScaleCount;
            }
        };
        applyTextureDefinition(scenario.Sky1);
        applyTextureDefinition(scenario.Sky2);
        if (scrollScaleCount > 0)
        {
            scenario.SkyScrollScale = scrollScaleSum / static_cast<float>(scrollScaleCount);
        }
        scenarioBySourceIndex[sourceIndex] = WeatherScenarios.size();
        WeatherScenarios.push_back(std::move(scenario));
    }

    for (const auto& section : sections)
    {
        int sourceScenarioIndex = -1;
        if (!ParseWeatherScenarioKeyframeSection(section.Name, sourceScenarioIndex))
        {
            continue;
        }
        const std::size_t scenarioIndex = scenarioBySourceIndex[sourceScenarioIndex];
        if (scenarioIndex == (std::numeric_limits<std::size_t>::max)())
        {
            continue;
        }
        const std::string* timeValue = WeatherValue(section, "t");
        const std::string* rainValue = WeatherValue(section, "r");
        if (!timeValue || !rainValue)
        {
            continue;
        }
        WeatherKeyframe frame;
        if (!ParseWeatherFloat(*timeValue, frame.Time) || !ParseWeatherFloat(*rainValue, frame.Rain))
        {
            continue;
        }
        frame.Rain = NormalizeWeatherLevel(frame.Rain);
        if (const std::string* value = WeatherValue(section, "fl1"))
        {
            float fog = 0.0f;
            if (ParseWeatherFloat(*value, fog))
            {
                frame.Fog = NormalizeWeatherLevel(fog);
            }
        }
        if (const std::string* value = WeatherValue(section, "fl2"))
        {
            float wind = 0.0f;
            if (ParseWeatherFloat(*value, wind))
            {
                frame.Wind = NormalizeWeatherLevel(wind);
            }
        }
        if (const std::string* value = WeatherValue(section, "cs"))
        {
            float cloud = 0.0f;
            if (ParseWeatherFloat(*value, cloud))
            {
                frame.Cloud = NormalizeWeatherLevel(cloud);
            }
        }
        WeatherScenarios[scenarioIndex].Keyframes.push_back(frame);
    }

    for (const auto& section : sections)
    {
        if (!IsIndexedWeatherSection(section.Name, 's', 3))
        {
            continue;
        }
        const std::string* value = WeatherValue(section, "s");
        int sourceScenarioIndex = -1;
        if (!value || !ParseWeatherInt(*value, sourceScenarioIndex) || sourceScenarioIndex < 0 || sourceScenarioIndex >= static_cast<int>(scenarioBySourceIndex.size()))
        {
            continue;
        }
        const std::size_t scenarioIndex = scenarioBySourceIndex[sourceScenarioIndex];
        if (scenarioIndex != (std::numeric_limits<std::size_t>::max)())
        {
            WeatherSequence.push_back(scenarioIndex);
        }
    }

    if (WeatherScenarios.empty())
    {
        WeatherScenarios = {
            {"clear", {}, {}, 150.0f, 0.12f, 0.0f, 0.0f, 0.35f, 0.8f, {}},
            {"overcast", {}, {}, 90.0f, 0.75f, 0.0f, 0.35f, 0.75f, 1.35f, {}},
            {"rain", {}, {}, 120.0f, 1.0f, 1.0f, 0.85f, 1.0f, 1.8f, {}},
            {"clearing", {}, {}, 75.0f, 0.45f, 0.15f, 0.2f, 0.55f, 1.1f, {}}
        };
    }
    if (WeatherSequence.empty())
    {
        WeatherSequence.resize(WeatherScenarios.size());
        for (std::size_t index = 0; index < WeatherScenarios.size(); ++index)
        {
            WeatherSequence[index] = index;
        }
    }

    for (std::size_t index = 0; index < WeatherScenarios.size(); ++index)
    {
        auto& scenario = WeatherScenarios[index];
        std::sort(scenario.Keyframes.begin(), scenario.Keyframes.end(), [](const WeatherKeyframe& left, const WeatherKeyframe& right) { return left.Time < right.Time; });
        if (!scenario.Keyframes.empty())
        {
            const float lastTime = scenario.Keyframes.back().Time;
            if (lastTime > scenario.Duration && lastTime > 0.0f)
            {
                const float scale = scenario.Duration / lastTime;
                for (auto& frame : scenario.Keyframes)
                {
                    frame.Time *= scale;
                }
            }
            else if (lastTime <= 1.0f)
            {
                for (auto& frame : scenario.Keyframes)
                {
                    frame.Time *= scenario.Duration;
                }
            }
            for (const auto& frame : scenario.Keyframes)
            {
                scenario.Rain = (std::max)(scenario.Rain, frame.Rain);
                scenario.Fog = (std::max)(scenario.Fog, frame.Fog);
                scenario.Wind = (std::max)(scenario.Wind, frame.Wind);
            }
        }
        scenario.CloudCover = (std::max)(scenario.CloudCover, scenario.Rain * 0.9f);
        scenario.Fog = (std::max)(scenario.Fog, scenario.Rain * 0.65f);
        scenario.Wind = (std::max)(scenario.Wind, 0.35f + scenario.Rain * 0.65f);
    }
    double totalDuration = 0.0;
    for (std::size_t index : WeatherSequence)
    {
        totalDuration += WeatherScenarios[index].Duration;
    }
    if (totalDuration > 0.0)
    {
        const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        double cursor = std::fmod(static_cast<double>(epoch), totalDuration);
        if (cursor < 0.0)
        {
            cursor += totalDuration;
        }
        for (std::size_t position = 0; position < WeatherSequence.size(); ++position)
        {
            const float duration = WeatherScenarios[WeatherSequence[position]].Duration;
            if (cursor < duration)
            {
                WeatherSequencePosition = position;
                WeatherScenarioElapsed = static_cast<float>(cursor);
                break;
            }
            cursor -= duration;
        }
    }

    const auto rainPath = ResolveWeatherTexturePath("fx_rain");
    if (!rainPath.empty())
    {
        try
        {
            RainTexture = LoadCachedDdsTexture(rainPath);
        }
        catch (const std::exception& exception)
        {
            if (Logger)
            {
                Logger->Warning("Rain texture load failed: " + std::string(exception.what()));
            }
        }
    }
    RefreshWeatherSkyTextures();
    UpdateWeather(0.0f);
    if (Logger)
    {
        Logger->Info("Weather system restored: source=" + WorldScene->Weather().SourceName + ", scenarios=" + std::to_string(WeatherScenarios.size()) + ", sequence=" + std::to_string(WeatherSequence.size()) + ", rain_texture=" + std::string(RainTexture ? "yes" : "procedural"));
    }
}

void FD3D9GameWorldScene::Impl::ApplyWeatherEnvironment()
{
    Environment = DayNightEnvironment;
    const float cloud = std::clamp(WeatherCloudCover, 0.0f, 1.0f);
    const float rain = std::clamp(WeatherRain, 0.0f, 1.0f);
    auto weatherChannel = [cloud, rain](int source, int overcast, float darken)
    {
        const float blend = std::clamp(cloud * 0.55f + rain * 0.2f, 0.0f, 0.85f);
        const float mixed = static_cast<float>(source) + (static_cast<float>(overcast) - static_cast<float>(source)) * blend;
        return static_cast<int>(std::lround(std::clamp(mixed * (1.0f - darken * rain), 0.0f, 255.0f)));
    };
    Environment.ClearRed = weatherChannel(DayNightEnvironment.ClearRed, 72, 0.08f);
    Environment.ClearGreen = weatherChannel(DayNightEnvironment.ClearGreen, 84, 0.08f);
    Environment.ClearBlue = weatherChannel(DayNightEnvironment.ClearBlue, 98, 0.06f);
    Environment.AmbientRed = weatherChannel(DayNightEnvironment.AmbientRed, 92, 0.12f);
    Environment.AmbientGreen = weatherChannel(DayNightEnvironment.AmbientGreen, 98, 0.12f);
    Environment.AmbientBlue = weatherChannel(DayNightEnvironment.AmbientBlue, 108, 0.1f);
    Environment.SunRed = weatherChannel(DayNightEnvironment.SunRed, 105, 0.3f);
    Environment.SunGreen = weatherChannel(DayNightEnvironment.SunGreen, 108, 0.3f);
    Environment.SunBlue = weatherChannel(DayNightEnvironment.SunBlue, 116, 0.25f);
    Environment.CloudRed = weatherChannel(DayNightEnvironment.CloudRed, 118, 0.04f);
    Environment.CloudGreen = weatherChannel(DayNightEnvironment.CloudGreen, 125, 0.04f);
    Environment.CloudBlue = weatherChannel(DayNightEnvironment.CloudBlue, 136, 0.02f);
    const float fog = std::clamp((std::max)(WeatherFog, rain * 0.7f), 0.0f, 1.0f);
    WeatherFogStart = Config.FogStart * (1.0f - fog * 0.48f);
    WeatherFogEnd = Config.FogEnd * (1.0f - fog * 0.42f);
}

void FD3D9GameWorldScene::Impl::BuildTerrainPathIndex() const
{
    if (TerrainPathIndexReady || !AssetResources)
    {
        return;
    }

    TerrainLndPathByRelativeKey.clear();
    TerrainLndPathByStemKey.clear();
    TerrainLndPathByRelativeKey.reserve(1024);
    TerrainLndPathByStemKey.reserve(1024);
    for (const auto& record : AssetResources->Catalog().All())
    {
        if (!IsTerrainLndPath(record.RelativePath))
        {
            continue;
        }
        const std::string relativeKey = Common::NormalizePathKey(record.RelativePath);
        const std::string stemKey = TerrainStemKey(record.RelativePath.stem().string());
        TerrainLndPathByRelativeKey.emplace(relativeKey, record.AbsolutePath);
        TerrainLndPathByStemKey.emplace(stemKey, record.AbsolutePath);
    }
    TerrainPathIndexReady = true;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainRelativePath(const std::filesystem::path& RelativePath) const
{
    if (!AssetResources || RelativePath.empty())
    {
        return std::nullopt;
    }

    const std::string key = Common::NormalizePathKey(RelativePath);
    if (const auto cached = TerrainRelativePathCache.find(key); cached != TerrainRelativePathCache.end())
    {
        return cached->second;
    }

    BuildTerrainPathIndex();
    if (const auto it = TerrainLndPathByRelativeKey.find(key); it != TerrainLndPathByRelativeKey.end())
    {
        TerrainRelativePathCache.emplace(key, it->second);
        return it->second;
    }

    const auto direct = AssetResources->Catalog().FindByLogicalName(RelativePath.generic_string());
    if (direct && IsTerrainLndPath(direct->RelativePath))
    {
        TerrainRelativePathCache.emplace(key, direct->AbsolutePath);
        return direct->AbsolutePath;
    }

    TerrainRelativePathCache.emplace(key, std::nullopt);
    return std::nullopt;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainStemPath(const std::string& TerrainStem) const
{
    if (!AssetResources || TerrainStem.empty())
    {
        return std::nullopt;
    }

    const std::string stemKey = TerrainStemKey(TerrainStem);
    if (const auto cached = TerrainStemPathCache.find(stemKey); cached != TerrainStemPathCache.end())
    {
        return cached->second;
    }

    BuildTerrainPathIndex();
    const std::array<std::string, 6> logicalCandidates
    {{
        "landscape/" + TerrainStem + ".lnd",
        "Landscape/" + TerrainStem + ".lnd",
        "Landscape_ph/" + TerrainStem + ".lnd",
        "Landscape_hr/" + TerrainStem + ".lnd",
        "Landscape_rd/" + TerrainStem + ".lnd",
        TerrainStem + ".lnd"
    }};

    for (const auto& logicalName : logicalCandidates)
    {
        const std::string key = Common::NormalizePathKey(logicalName);
        if (const auto it = TerrainLndPathByRelativeKey.find(key); it != TerrainLndPathByRelativeKey.end())
        {
            TerrainStemPathCache.emplace(stemKey, it->second);
            return it->second;
        }
    }

    if (const auto it = TerrainLndPathByStemKey.find(stemKey); it != TerrainLndPathByStemKey.end())
    {
        TerrainStemPathCache.emplace(stemKey, it->second);
        return it->second;
    }

    TerrainStemPathCache.emplace(stemKey, std::nullopt);
    return std::nullopt;
}

std::optional<std::filesystem::path> FD3D9GameWorldScene::Impl::TryResolveTerrainPathFromPatch(const FWorldPatchRecord& PatchRecord) const
{
    FPath candidate = PatchRecord.RelativePath;
    candidate.replace_extension(".lnd");
    if (const auto sibling = TryResolveTerrainRelativePath(candidate))
    {
        return sibling;
    }

    if (!PatchRecord.StemName.empty())
    {
        if (const auto byStem = TryResolveTerrainStemPath(PatchRecord.StemName))
        {
            return byStem;
        }
    }

    if (!PatchRecord.Name.empty())
    {
        if (const auto byName = TryResolveTerrainStemPath(Common::StripExtension(PatchRecord.Name)))
        {
            return byName;
        }
    }

    return std::nullopt;
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveTerrainPath(const FWorldMapCell& cell) const
{
    const std::string terrainStem = cell.TerrainStem();
    auto resolveTerrainSizePath = [this](const FWorldTerrainSizeRecord& terrain) -> std::filesystem::path
    {
        FPath lndPath = terrain.RelativePath;
        lndPath.replace_extension(".lnd");
        if (const auto terrainPath = TryResolveTerrainRelativePath(lndPath))
        {
            return *terrainPath;
        }
        return ResolveConfiguredPath(lndPath.generic_string());
    };

    if (WorldScene)
    {
        if (cell.ResolvedByTerrainSize && cell.TerrainSizeRecordIndex < WorldScene->TerrainSizeRecords().size())
        {
            return resolveTerrainSizePath(WorldScene->TerrainSizeRecords()[cell.TerrainSizeRecordIndex]);
        }

        if (!terrainStem.empty())
        {
            if (const FWorldTerrainSizeRecord* terrain = WorldScene->FindTerrainSizeByName(terrainStem))
            {
                return resolveTerrainSizePath(*terrain);
            }

            if (const auto terrainPath = TryResolveTerrainStemPath(terrainStem))
            {
                return *terrainPath;
            }

            if (const FWorldPatchRecord* patch = WorldScene->FindPatchByName(terrainStem))
            {
                if (const auto terrainPath = TryResolveTerrainPathFromPatch(*patch))
                {
                    return *terrainPath;
                }
            }
        }

        if (const FWorldTerrainSizeRecord* terrain = WorldScene->FindTerrainSizeByName(cell.TileName))
        {
            return resolveTerrainSizePath(*terrain);
        }

        if (const auto terrainPath = TryResolveTerrainStemPath(cell.TileName))
        {
            return *terrainPath;
        }

        if (cell.ResolvedByPatchCatalog && cell.TileRecordIndex < WorldScene->Patches().size())
        {
            if (const auto terrainPath = TryResolveTerrainPathFromPatch(WorldScene->Patches()[cell.TileRecordIndex]))
            {
                return *terrainPath;
            }
        }

        if (const FWorldPatchRecord* patch = WorldScene->FindPatchByName(cell.TileName))
        {
            if (const auto terrainPath = TryResolveTerrainPathFromPatch(*patch))
            {
                return *terrainPath;
            }
        }
    }

    if (!terrainStem.empty())
    {
        if (const auto terrainPath = TryResolveTerrainStemPath(terrainStem))
        {
            return *terrainPath;
        }
    }

    if (const auto terrainPath = TryResolveTerrainStemPath(cell.TileName))
    {
        return *terrainPath;
    }

    throw std::runtime_error("required landscape tile is missing: " + (terrainStem.empty() ? cell.TileName : terrainStem) + ".lnd");
}
void FD3D9GameWorldScene::Impl::BuildModelPathIndex() const
{
    if (ModelPathIndexReady || !AssetResources)
    {
        return;
    }
    ModelPathIndex.clear();
    std::vector<std::string> dirs;
    dirs.reserve(Config.ModelDirs.size());
    for (const auto& dir : Config.ModelDirs)
    {
        auto key = Common::NormalizePathKey(NarrowAscii(dir));
        if (!key.empty())
        {
            dirs.push_back(std::move(key));
        }
    }
    std::unordered_map<std::string, std::size_t> priorities;
    priorities.reserve(512);
    for (const auto& record : AssetResources->Catalog().All())
    {
        const std::string rel = Common::NormalizePathKey(record.RelativePath);
        if (!rel.ends_with(".mdl"))
        {
            continue;
        }
        std::size_t priority = 0;
        bool allowed = dirs.empty();
        for (std::size_t i = 0; i < dirs.size(); ++i)
        {
            const auto& dir = dirs[i];
            const std::string prefix = dir + "/";
            if (rel.starts_with(prefix) || rel.find("/" + prefix) != std::string::npos)
            {
                allowed = true;
                priority = i;
                break;
            }
        }
        if (!allowed)
        {
            continue;
        }
        const auto modelKey = Common::BaseNameWithoutExtension(rel);
        if (modelKey.empty())
        {
            continue;
        }
        const auto priorityIt = priorities.find(modelKey);
        if (priorityIt == priorities.end() || priority < priorityIt->second)
        {
            priorities[modelKey] = priority;
            ModelPathIndex[modelKey] = record.AbsolutePath;
        }
    }
    ModelPathIndexReady = true;
}


std::filesystem::path FD3D9GameWorldScene::Impl::ResolveModelPath(const std::string& ModelName) const
{
    const auto cacheKey = LowercaseAscii(ModelName);
    if (const auto cached = ModelPathCache.find(cacheKey); cached != ModelPathCache.end())
    {
        if (!cached->second.empty())
        {
            return cached->second;
        }
        throw std::runtime_error("required static model is missing: " + ModelName + ".mdl");
    }
    BuildModelPathIndex();
    if (const auto indexed = ModelPathIndex.find(cacheKey); indexed != ModelPathIndex.end())
    {
        ModelPathCache.emplace(cacheKey, indexed->second);
        return indexed->second;
    }
    for (const auto& dir : Config.ModelDirs)
    {
        const auto logical = NarrowAscii(dir) + "/" + ModelName + ".mdl";
        const auto path = ResolveOptionalPath(logical);
        if (!path.empty())
        {
            ModelPathCache.emplace(cacheKey, path);
            return path;
        }
    }
    ModelPathCache.emplace(cacheKey, std::filesystem::path{});
    throw std::runtime_error("required static model is missing: " + ModelName + ".mdl");
}

std::filesystem::path FD3D9GameWorldScene::Impl::ResolveModelTexturePath(
    const std::filesystem::path& ModelPath,
    const std::string& MaterialName) const
{
    std::lock_guard<std::mutex> cacheLock(ModelTexturePathCacheMutex);
    const auto TextureName = LowercaseAscii(MaterialName) + ".dds";
    const auto cacheKey = Common::NormalizePathKey(ModelPath.generic_string() + "|" + TextureName);
    if (const auto cached = ModelTexturePathCache.find(cacheKey); cached != ModelTexturePathCache.end())
    {
        if (!cached->second.empty())
        {
            return cached->second;
        }
        throw std::runtime_error("required static model texture is missing: " + MaterialName + ".dds for " + ModelPath.string());
    }
    const auto LocalPath = ModelPath.parent_path() / "textures" / TextureName;
    if (std::filesystem::exists(LocalPath))
    {
        ModelTexturePathCache.emplace(cacheKey, LocalPath);
        return LocalPath;
    }
    for (const auto& dir : Config.ModelDirs)
    {
        const auto logical = NarrowAscii(dir) + "/textures/" + TextureName;
        const auto path = ResolveOptionalPath(logical);
        if (!path.empty())
        {
            ModelTexturePathCache.emplace(cacheKey, path);
            return path;
        }
    }
    const auto fallback = ResolveOptionalPath(TextureName);
    if (!fallback.empty())
    {
        ModelTexturePathCache.emplace(cacheKey, fallback);
        return fallback;
    }
    ModelTexturePathCache.emplace(cacheKey, std::filesystem::path{});
    throw std::runtime_error("required static model texture is missing: " + MaterialName + ".dds for " + ModelPath.string());
}

IDirect3DTexture9* FD3D9GameWorldScene::Impl::LoadCachedDdsTexture(const std::filesystem::path& Path)
{
    const auto key = Path.lexically_normal().wstring();
    if (auto it = DdsTextureCache.find(key); it != DdsTextureCache.end())
    {
        it->second->AddRef();
        return it->second;
    }
    return LoadCachedDdsTextureFromBytes(Path, ReadGameWorldFileBytes(Path));
}

IDirect3DTexture9* FD3D9GameWorldScene::Impl::LoadCachedDdsTextureFromBytes(const std::filesystem::path& Path, const FByteArray& Bytes)
{
    const auto key = Path.lexically_normal().wstring();
    if (auto it = DdsTextureCache.find(key); it != DdsTextureCache.end())
    {
        it->second->AddRef();
        return it->second;
    }
    IDirect3DTexture9* texture = CreateD3D9TextureFromDdsBytes(Device, Bytes, Path.string());
    DdsTextureCache.emplace(key, texture);
    texture->AddRef();
    return texture;
}

bool FD3D9GameWorldScene::Impl::Initialize(
    HWND window,
    IDirect3DDevice9* ExternalDevice,
    const FResourceManager& ResourceManager,
    const FWorldScene& world,
    const FGameWorldConfig& WorldConfig,
    double x,
    double y,
    double z,
    double Angle,
    std::wstring& error,
    FLogger* InLogger,
    const FSkinnedCharacterModel* PlayerModelIn)
{
    Release();
    Hwnd = window;
    Device = ExternalDevice;
    if (Device)
    {
        Device->AddRef();
    }
    AssetResources = &ResourceManager;
    WorldScene = &world;
    Logger = InLogger;
    Config = WorldConfig;
    RemotePlayers.reserve(256);
    RemoteActors.reserve(1024);
    VisibleAnimatedResources.reserve(128);
    QueuedTerrainCpuPreloadCells.reserve(2048);
    PendingTerrainCpuPreloads.reserve(256);
    StartTerrainCpuPreloadWorker();
    StartStaticModelCpuPreloadWorker();
    StartGrassMapPreloadWorker();
    DayNightEnvironment = FGameWorldSkyState{0.0f, Config.ClearRed, Config.ClearGreen, Config.ClearBlue, 110, 110, 110, 255, 245, 224, Config.SkyRed, Config.SkyGreen, Config.SkyBlue};
    Environment = DayNightEnvironment;
    WeatherFogStart = Config.FogStart;
    WeatherFogEnd = Config.FogEnd;
    SpawnX = static_cast<float>(x);
    SpawnY = static_cast<float>(y);
    SpawnZ = static_cast<float>(z);
    SpawnAngle = static_cast<float>(Angle);
    CameraYaw = -SpawnAngle;
    if (!Device)
    {
        error = L"D3D9 game world received null device";
        return false;
    }
    FillPresentParameters();
    try
    {
        BuildTerrainPathIndex();
        BuildModelPathIndex();
        BuildMonsterModelIndex();
        LoadWorldShaders();
        TerrainMicrotexture = LoadMtxTexture(Device, ResolveConfiguredPath(NarrowAscii(Config.TerrainMicrotexture)));
        SkyTexture = LoadCachedDdsTexture(ResolveConfiguredPath(NarrowAscii(Config.SkyTexture)));
        LoadWeatherSystem();
        const auto WaterPath = ResolveOptionalPath("landscape/river1a_00.dds");
        if (!WaterPath.empty())
        {
            WaterTexture = LoadCachedDdsTexture(WaterPath);
        }
        TerrainInitialBlockingLoad = true;
        LoadVisibleTerrain();
        TerrainInitialBlockingLoad = false;
        for (auto& [_, resource] : TerrainResources)
        {
            if (resource)
            {
                UploadTerrainGpuResource(*resource);
            }
        }
        PendingTerrainGpuPromotions.clear();
        QueuedTerrainGpuPromotions.clear();
        SnapToGround();
        LoadStaticPlacements();
        StaticInitialBlockingLoad = true;
        LoadVisibleStaticObjects();
        StaticInitialBlockingLoad = false;
        DrainStaticRenderCellBakeJobs(true);
        SnapToGround();
        GrassInitialBlockingLoad = true;
        LoadVisibleGrass();
        DrainGrassRenderBakeJobs(true);
        GrassInitialBlockingLoad = false;
        PreloadStreamingGuard();
        if (PlayerModelIn && PlayerModelIn->IsValid())
        {
            LoadPlayerModel(*PlayerModelIn);
        }
        CreateReflectionTarget();
    }
    catch (const std::exception& ex)
    {
        TerrainInitialBlockingLoad = false;
        StaticInitialBlockingLoad = false;
        GrassInitialBlockingLoad = false;
        AssignError(error, std::string("game world load failed: ") + ex.what());
        return false;
    }
    ConfigureRenderState();
    Initialized = true;
    return true;
}

bool FD3D9GameWorldScene::Impl::SetOverlayBitmap(int width, int height, std::vector<uint8> pixels, std::wstring& error)
{
    if (!Device)
    {
        error = L"SetOverlayBitmap called before Direct3D device creation";
        return false;
    }
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width) * height * 4)
    {
        error = L"invalid game overlay bitmap";
        return false;
    }
    HRESULT hr = S_OK;
    if (!OverlayTexture || OverlayWidth != width || OverlayHeight != height)
    {
        SafeRelease(OverlayTexture);
        hr = Device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &OverlayTexture, nullptr);
        if (FAILED(hr))
        {
            error = HResultText("CreateTexture game overlay", hr);
            return false;
        }
    }
    D3DLOCKED_RECT locked{};
    hr = OverlayTexture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(hr))
    {
        error = HResultText("GameOverlayTexture::LockRect", hr);
        return false;
    }
    const std::size_t SourcePitch = static_cast<std::size_t>(width) * 4;
    for (int row = 0; row < height; ++row)
    {
        std::copy_n(
        pixels.data() + static_cast<std::size_t>(row) * SourcePitch,
        SourcePitch,
        static_cast<uint8*>(locked.pBits) + static_cast<std::size_t>(row) * locked.Pitch);
    }
    OverlayTexture->UnlockRect(0);
    OverlayWidth = width;
    OverlayHeight = height;
    return true;
}

void FD3D9GameWorldScene::Impl::SetFog(float start, float end)
{
    Config.FogStart = start;
    Config.FogEnd = end;
    ApplyWeatherEnvironment();
    if (Device)
    {
        ConfigureRenderState();
    }
}

bool FD3D9GameWorldScene::Impl::SetGrassQuality(int quality, std::wstring& error)
{
    quality = std::clamp(quality, 0, 2);
    if (Config.GrassQuality == quality)
    {
        return true;
    }
    Config.GrassQuality = quality;
    ClearGrassRenderBatches();
    GrassInstancesByCell.clear();
    GrassInstanceCount = 0;
    GrassCells.clear();
    GrassTargetCells.clear();
    GrassPendingCells.clear();
    GrassCenterX = (std::numeric_limits<int>::min)();
    GrassCenterZ = (std::numeric_limits<int>::min)();
    GrassAnchorValid = false;
    try
    {
        LoadVisibleGrass();
        return true;
    }
    catch (const std::exception& ex)
    {
        AssignError(error, std::string("game world grass quality update failed: ") + ex.what());
        return false;
    }
}

void FD3D9GameWorldScene::Impl::SetGameTime(float DayFraction)
{
    GameTimeFraction = DayFraction - std::floor(DayFraction);
    const auto& states = Config.SkyStates;
    if (states.size() < 2)
    {
        return;
    }
    std::size_t next = 0;
    while (next < states.size() && GameTimeFraction >= states[next].Time)
    {
        ++next;
    }
    const FGameWorldSkyState* from = nullptr;
    const FGameWorldSkyState* to = nullptr;
    float FromTime = 0.0f;
    float ToTime = 0.0f;
    if (next == 0)
    {
        from = &states.back();
        to = &states.front();
        FromTime = states.back().Time - 1.0f;
        ToTime = states.front().Time;
    } else if (next == states.size())
    {
        from = &states.back();
        to = &states.front();
        FromTime = states.back().Time;
        ToTime = states.front().Time + 1.0f;
    } else
    {
        from = &states[next - 1];
        to = &states[next];
        FromTime = from->Time;
        ToTime = to->Time;
    }
    float SampleTime = GameTimeFraction;
    if (SampleTime < FromTime)
    {
        SampleTime += 1.0f;
    }
    const float blend = std::clamp((SampleTime - FromTime) / (ToTime - FromTime), 0.0f, 1.0f);
    auto channel = [blend](int a, int b)
    {
        return static_cast<int>(std::lround(static_cast<float>(a) + static_cast<float>(b - a) * blend));
    };
    for (const auto field : {&FGameWorldSkyState::ClearRed, &FGameWorldSkyState::ClearGreen, &FGameWorldSkyState::ClearBlue, &FGameWorldSkyState::AmbientRed, &FGameWorldSkyState::AmbientGreen, &FGameWorldSkyState::AmbientBlue, &FGameWorldSkyState::SunRed, &FGameWorldSkyState::SunGreen, &FGameWorldSkyState::SunBlue, &FGameWorldSkyState::CloudRed, &FGameWorldSkyState::CloudGreen, &FGameWorldSkyState::CloudBlue})
    {
        DayNightEnvironment.*field = channel(from->*field, to->*field);
    }
    ApplyWeatherEnvironment();
    if (Device)
    {
        ConfigureRenderState();
    }

}
