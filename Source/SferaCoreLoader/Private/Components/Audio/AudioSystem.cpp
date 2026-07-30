#include "Components/Audio/AudioSystem.h"
#include "Components/Audio/LegacyAudioDefinitionLoader.h"
#include "Components/Audio/LegacyAudioResolver.h"
#include "Core/Logger.h"
#include "ResourceLoader/ResourceManager.h"
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace
{
    constexpr size_t AudioBusCount = static_cast<size_t>(EAudioBus::Count);
    constexpr size_t MaxEncodedAudioBytes = 512ull * 1024ull * 1024ull;
    constexpr size_t MaxDecodedAudioSamples = 512ull * 1024ull * 1024ull / sizeof(int16);
    constexpr ma_uint64 DecodeChunkFrames = 4096;
    constexpr float FullCircle = 6.28318530717958647692f;

    std::string NormalizeAudioName(std::string_view value)
    {
        std::string result(value);
        for (char& c : result)
        {
            if (c == '/') { c = '\\'; }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    }

    bool IsHourInRange(float hour, float start, float end)
    {
        if (hour < 0.0f || start < 0.0f || end < 0.0f) { return true; }
        hour = std::fmod(hour, 24.0f);
        if (hour < 0.0f) { hour += 24.0f; }
        return start <= end ? hour >= start && hour < end : hour >= start || hour < end;
    }

    FAudioCueDefinition MakeUiCue(std::string asset)
    {
        FAudioCueDefinition cue;
        cue.Bus = EAudioBus::Ui;
        cue.Variants.push_back(FAudioCueVariant{std::move(asset)});
        cue.MinRetriggerSeconds = 0.015f;
        cue.CacheLifetimeSeconds = 300.0f;
        return cue;
    }

    std::string_view EventCueName(EAudioEvent event)
    {
        static constexpr std::array<std::string_view, static_cast<size_t>(EAudioEvent::Count)> names
        {
            "ui.click",
            "ui.link",
            "ui.line",
            "ui.page",
            "ui.window.open",
            "ui.window.close"
        };
        const size_t index = static_cast<size_t>(event);
        return index < names.size() ? names[index] : std::string_view{};
    }
}

class FAudioSystem::FImpl
{
public:
    struct FDecodedAsset
    {
        std::string LogicalName;
        std::vector<int16> Samples;
        uint32 Channels = 0;
        uint32 SampleRate = 0;
        uint64 FrameCount = 0;
    };

    struct FEncodedAsset
    {
        std::string LogicalName;
        FByteArray Bytes;
    };

    struct FCachedDecodedAsset
    {
        std::shared_ptr<FDecodedAsset> Asset;
        double LastUsedSeconds = 0.0;
        float LifetimeSeconds = 60.0f;
    };

    struct FCueEntry
    {
        FAudioCueDefinition Definition;
        int32 LastVariant = -1;
        size_t SequenceIndex = 0;
        double LastPlaySeconds = -1.0;
    };

    struct FSource
    {
        uint64 Id = 0;
        FAudioHandle PlaybackHandle;
        EAudioBus Bus = EAudioBus::Effects;
        std::shared_ptr<FDecodedAsset> DecodedAsset;
        std::shared_ptr<FEncodedAsset> EncodedAsset;
        ma_audio_buffer Buffer{};
        ma_decoder Decoder{};
        ma_sound Sound{};
        FAudioVector3 FollowOffset;
        float BaseVolume = 1.0f;
        float FadeGain = 1.0f;
        float FadeTarget = 1.0f;
        float FadeSpeed = 0.0f;
        double NominalDurationSeconds = 0.0;
        bool BufferInitialized = false;
        bool DecoderInitialized = false;
        bool SoundInitialized = false;
        bool StopWhenSilent = false;
        bool Spatialized = false;
    };

    struct FPlayback
    {
        FAudioHandle Handle;
        std::string CueName;
        FAudioPlayContext Context;
        EAudioBus Bus = EAudioBus::Effects;
        std::vector<uint64> SourceIds;
        double CreatedSeconds = 0.0;
        double NextTriggerSeconds = -1.0;
        bool RepeatVariants = false;
        bool StopRequested = false;
        bool FinishAfterDelay = false;
    };

    struct FMusicRuntime
    {
        std::string Name;
        FMusicTrackDefinition Definition;
        std::shared_ptr<FEncodedAsset> Asset;
        std::unique_ptr<FSource> Source;
        int32 PatternIndex = 0;
        size_t CommandIndex = 0;
        double WaitUntilSeconds = -1.0;
        double SampleEndSeconds = -1.0;
        float RequestVolume = 1.0f;
        float FadeGain = 1.0f;
        float FadeTarget = 1.0f;
        float FadeSpeed = 0.0f;
        bool PlayingSample = false;
        bool WholeTrack = false;
        bool StopWhenSilent = false;
        bool Finished = false;
    };

    FStatus Initialize(const FResourceManager& resources, FLogger* logger)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Initialized) { return FStatus::Ok(); }
        Resources = &resources;
        Log = logger;
        ma_engine_config config = ma_engine_config_init();
        const ma_result engineResult = ma_engine_init(&config, &Engine);
        if (engineResult != MA_SUCCESS) { return FStatus::Error(EStatusCode::RuntimeError, "miniaudio engine initialization failed: " + std::to_string(engineResult)); }
        EngineInitialized = true;
        for (size_t index = 0; index < Groups.size(); ++index)
        {
            const ma_result groupResult = ma_sound_group_init(&Engine, 0, nullptr, &Groups[index]);
            if (groupResult != MA_SUCCESS)
            {
                ShutdownLocked();
                return FStatus::Error(EStatusCode::RuntimeError, "miniaudio sound group initialization failed: " + std::to_string(groupResult));
            }
            ++InitializedGroupCount;
        }
        Initialized = true;
        if (Log) { Log->Info("audio runtime initialized: miniaudio, buses=5"); }
        return FStatus::Ok();
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(Mutex);
        ShutdownLocked();
    }

    void ShutdownLocked()
    {
        for (auto& source : ActiveSources) { DestroySource(*source); }
        ActiveSources.clear();
        Playbacks.clear();
        for (auto& music : MusicStates) { if (music->Source) { DestroySource(*music->Source); } }
        MusicStates.clear();
        CurrentMusicName.clear();
        DecodedCache.clear();
        EncodedCache.clear();
        Cues.clear();
        MusicTracks.clear();
        LegacyEffects.clear();
        MissingMessages.clear();
        while (InitializedGroupCount > 0)
        {
            --InitializedGroupCount;
            ma_sound_group_uninit(&Groups[InitializedGroupCount]);
        }
        if (EngineInitialized) { ma_engine_uninit(&Engine); }
        EngineInitialized = false;
        Initialized = false;
        Resources = nullptr;
        if (Log) { Log->Info("audio runtime shut down"); }
        Log = nullptr;
    }

    void RegisterDefaults()
    {
        RegisterCue("ui.click", MakeUiCue("Sounds\\in_click.wav"));
        RegisterCue("ui.link", MakeUiCue("Sounds\\in_link.wav"));
        RegisterCue("ui.line", MakeUiCue("Sounds\\in_line.wav"));
        RegisterCue("ui.page", MakeUiCue("Sounds\\in_page.wav"));
        RegisterCue("ui.window.open", MakeUiCue("Sounds\\in_winopen.wav"));
        RegisterCue("ui.window.close", MakeUiCue("Sounds\\in_winclose.wav"));
    }

    FAudioImportReport ImportLegacyDefinitions(FAudioSystem& owner)
    {
        const FResourceManager* resources = nullptr;
        FLogger* logger = nullptr;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            resources = Resources;
            logger = Log;
        }
        return resources ? FLegacyAudioDefinitionLoader::Import(*resources, owner, logger) : FAudioImportReport{};
    }

    void RegisterCue(std::string name, FAudioCueDefinition definition)
    {
        if (name.empty() || definition.Variants.empty()) { return; }
        definition.MinDistance = std::max(0.01f, definition.MinDistance);
        definition.MaxDistance = std::max(definition.MinDistance, definition.MaxDistance);
        definition.Volume = std::max(0.0f, definition.Volume);
        definition.Pitch = std::max(0.01f, definition.Pitch);
        std::lock_guard<std::mutex> lock(Mutex);
        Cues[NormalizeAudioName(name)] = FCueEntry{std::move(definition)};
    }

    void RegisterMusicTrack(std::string name, FMusicTrackDefinition definition)
    {
        if (name.empty() || definition.Asset.empty()) { return; }
        definition.Volume = std::max(0.0f, definition.Volume);
        std::lock_guard<std::mutex> lock(Mutex);
        MusicTracks[NormalizeAudioName(name)] = std::move(definition);
    }

    bool RegisterMusicTrackIfMissing(std::string name, FMusicTrackDefinition definition)
    {
        if (name.empty() || definition.Asset.empty()) { return false; }
        definition.Volume = std::max(0.0f, definition.Volume);
        std::lock_guard<std::mutex> lock(Mutex);
        return MusicTracks.emplace(NormalizeAudioName(name), std::move(definition)).second;
    }

    void RegisterLegacyEffect(int32 effectId, std::string cueName)
    {
        if (cueName.empty()) { return; }
        std::lock_guard<std::mutex> lock(Mutex);
        LegacyEffects[effectId] = NormalizeAudioName(cueName);
    }

    FAudioHandle PlayCue(std::string_view cueName, const FAudioPlayContext& context)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!Initialized || !Resources) { return {}; }
        const std::string normalizedName = NormalizeAudioName(cueName);
        auto cueIt = Cues.find(normalizedName);
        if (cueIt == Cues.end()) { LogMissing("audio cue is not registered: " + normalizedName); return {}; }
        FCueEntry& cue = cueIt->second;
        if (cue.Definition.MinRetriggerSeconds > 0.0f && cue.LastPlaySeconds >= 0.0 && ClockSeconds - cue.LastPlaySeconds < cue.Definition.MinRetriggerSeconds) { return {}; }
        if (cue.Definition.MaxInstances > 0 && CountCueInstances(normalizedName) >= cue.Definition.MaxInstances) { RemoveOldestPlayback(normalizedName); }
        auto playback = std::make_unique<FPlayback>();
        playback->Handle = AllocateHandle();
        playback->CueName = normalizedName;
        playback->Context = context;
        if (playback->Context.GameTimeHours < 0.0f) { playback->Context.GameTimeHours = GameTimeHours; }
        playback->Bus = context.BusOverride.value_or(cue.Definition.Bus);
        playback->CreatedSeconds = ClockSeconds;
        playback->RepeatVariants = cue.Definition.RepeatVariants && !cue.Definition.Loop;
        FPlayback& playbackRef = *playback;
        Playbacks.push_back(std::move(playback));
        if (!TriggerCue(playbackRef, cue)) { RemovePlayback(playbackRef.Handle); return {}; }
        cue.LastPlaySeconds = ClockSeconds;
        return playbackRef.Handle;
    }

    FAudioHandle PlayLegacyEffect(int32 effectId, const FAudioPlayContext& context)
    {
        std::string cueName;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            const auto it = LegacyEffects.find(effectId);
            cueName = it != LegacyEffects.end() ? it->second : FLegacyAudioResolver::CueNameForEffect(effectId);
        }
        return PlayCue(cueName, context);
    }

    bool Stop(FAudioHandle handle, float fadeSeconds)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        FPlayback* playback = FindPlayback(handle);
        if (!playback) { return false; }
        playback->StopRequested = true;
        playback->NextTriggerSeconds = -1.0;
        for (uint64 sourceId : playback->SourceIds)
        {
            if (FSource* source = FindSource(sourceId)) { BeginFade(*source, 0.0f, fadeSeconds, true); }
        }
        if (fadeSeconds <= 0.0f || playback->SourceIds.empty()) { RemovePlayback(handle); }
        return true;
    }

    void StopBus(EAudioBus bus, float fadeSeconds)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        std::vector<FAudioHandle> handles;
        for (const auto& playback : Playbacks) { if (playback->Bus == bus) { handles.push_back(playback->Handle); } }
        for (FAudioHandle handle : handles)
        {
            FPlayback* playback = FindPlayback(handle);
            if (!playback) { continue; }
            playback->StopRequested = true;
            playback->NextTriggerSeconds = -1.0;
            for (uint64 sourceId : playback->SourceIds) { if (FSource* source = FindSource(sourceId)) { BeginFade(*source, 0.0f, fadeSeconds, true); } }
            if (fadeSeconds <= 0.0f || playback->SourceIds.empty()) { RemovePlayback(handle); }
        }
        if (bus == EAudioBus::Music) { BeginMusicFadeOut(fadeSeconds); }
    }

    bool SetSourceTransform(FAudioHandle handle, const FAudioVector3& position, const FAudioVector3& velocity)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        FPlayback* playback = FindPlayback(handle);
        if (!playback) { return false; }
        playback->Context.Position = position;
        playback->Context.Velocity = velocity;
        for (uint64 sourceId : playback->SourceIds)
        {
            FSource* source = FindSource(sourceId);
            if (!source || !source->SoundInitialized || !source->Spatialized) { continue; }
            const FAudioVector3 finalPosition = position + source->FollowOffset;
            ma_sound_set_position(&source->Sound, finalPosition.X, finalPosition.Y, finalPosition.Z);
            ma_sound_set_velocity(&source->Sound, velocity.X, velocity.Y, velocity.Z);
        }
        return true;
    }

    void PlayMusic(const FMusicRequest& request)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!Initialized || !Resources) { return; }
        if (request.Cue.empty()) { BeginMusicFadeOut(request.FadeSeconds); CurrentMusicName.clear(); return; }
        const std::string name = NormalizeAudioName(request.Cue);
        if (name == CurrentMusicName && !MusicStates.empty() && !MusicStates.back()->Finished)
        {
            FMusicRuntime& current = *MusicStates.back();
            current.RequestVolume = std::max(0.0f, request.Volume);
            current.StopWhenSilent = false;
            current.FadeTarget = 1.0f;
            current.FadeSpeed = request.FadeSeconds > 0.0f ? std::abs(1.0f - current.FadeGain) / request.FadeSeconds : 0.0f;
            if (request.FadeSeconds <= 0.0f) { current.FadeGain = 1.0f; }
            ApplyMusicVolume(current);
            return;
        }
        const auto trackIt = MusicTracks.find(name);
        if (trackIt == MusicTracks.end())
        {
            LogMissing("music track is not registered: " + name);
            return;
        }
        if (Log) { Log->Info("music track resolved: cue=" + name + ", asset=" + trackIt->second.Asset); }
        std::shared_ptr<FEncodedAsset> asset = LoadEncodedAsset(trackIt->second.Asset);
        if (!asset) { return; }
        BeginMusicFadeOut(request.FadeSeconds);
        auto music = std::make_unique<FMusicRuntime>();
        music->Name = name;
        music->Definition = trackIt->second;
        music->Asset = std::move(asset);
        music->PatternIndex = music->Definition.StartPattern;
        music->RequestVolume = std::max(0.0f, request.Volume);
        if (request.FadeSeconds > 0.0f)
        {
            music->FadeGain = 0.0f;
            music->FadeTarget = 1.0f;
            music->FadeSpeed = 1.0f / request.FadeSeconds;
        }
        FMusicRuntime& musicRef = *music;
        MusicStates.push_back(std::move(music));
        CurrentMusicName = name;
        if (musicRef.Definition.Patterns.empty())
        {
            musicRef.Source = CreateStreamingSource(musicRef.Asset, musicRef.Definition.Volume * musicRef.RequestVolume * musicRef.FadeGain);
            if (!musicRef.Source) { musicRef.Finished = true; return; }
            musicRef.WholeTrack = true;
            ma_sound_set_looping(&musicRef.Source->Sound, musicRef.Definition.AutoFree ? MA_FALSE : MA_TRUE);
            ApplyMusicVolume(musicRef);
            if (ma_sound_start(&musicRef.Source->Sound) != MA_SUCCESS) { musicRef.Finished = true; }
        }
        else { ExecuteMusic(musicRef); }
    }

    void StopMusic(float fadeSeconds)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        BeginMusicFadeOut(fadeSeconds);
        CurrentMusicName.clear();
    }

    void SetListener(const FAudioListenerState& listener)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!Initialized) { return; }
        ma_engine_listener_set_position(&Engine, 0, listener.Position.X, listener.Position.Y, listener.Position.Z);
        ma_engine_listener_set_direction(&Engine, 0, listener.Forward.X, listener.Forward.Y, listener.Forward.Z);
        ma_engine_listener_set_world_up(&Engine, 0, listener.Up.X, listener.Up.Y, listener.Up.Z);
        ma_engine_listener_set_velocity(&Engine, 0, listener.Velocity.X, listener.Velocity.Y, listener.Velocity.Z);
    }

    void SetGameTimeHours(float hour)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!std::isfinite(hour) || hour < 0.0f) { GameTimeHours = -1.0f; return; }
        GameTimeHours = std::fmod(hour, 24.0f);
        if (GameTimeHours < 0.0f) { GameTimeHours += 24.0f; }
    }

    void SetMasterVolume(float volume)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Initialized) { ma_engine_set_volume(&Engine, std::max(0.0f, volume)); }
    }

    void SetBusVolume(EAudioBus bus, float volume)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        const size_t index = static_cast<size_t>(bus);
        if (Initialized && index < Groups.size()) { ma_sound_group_set_volume(&Groups[index], std::max(0.0f, volume)); }
    }

    void ClearCache()
    {
        std::lock_guard<std::mutex> lock(Mutex);
        for (auto it = DecodedCache.begin(); it != DecodedCache.end();)
        {
            if (it->second.Asset.use_count() == 1) { it = DecodedCache.erase(it); }
            else { ++it; }
        }
        for (auto it = EncodedCache.begin(); it != EncodedCache.end();)
        {
            if (it->second.use_count() == 1) { it = EncodedCache.erase(it); }
            else { ++it; }
        }
    }

    void Update(float deltaSeconds)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!Initialized) { return; }
        const float delta = std::clamp(deltaSeconds, 0.0f, 0.25f);
        ClockSeconds += delta;
        UpdateSources(delta);
        UpdatePlaybacks();
        UpdateMusic(delta);
        ExpireCache();
    }

    bool IsInitialized() const
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return Initialized;
    }

private:
    FAudioHandle AllocateHandle()
    {
        FAudioHandle handle{NextHandle++};
        if (NextHandle == 0) { NextHandle = 1; }
        return handle;
    }

    int32 SelectVariant(FCueEntry& cue, float gameTimeHours)
    {
        std::vector<int32> candidates;
        candidates.reserve(cue.Definition.Variants.size());
        for (size_t index = 0; index < cue.Definition.Variants.size(); ++index)
        {
            const FAudioCueVariant& variant = cue.Definition.Variants[index];
            if ((!variant.Asset.empty() || variant.SilenceSeconds > 0.0f) && IsHourInRange(gameTimeHours, variant.StartHour, variant.EndHour)) { candidates.push_back(static_cast<int32>(index)); }
        }
        if (candidates.empty()) { return -1; }
        if (cue.Definition.Selection == EAudioSelectionMode::First) { return candidates.front(); }
        if (cue.Definition.Selection == EAudioSelectionMode::Sequence)
        {
            const int32 selected = candidates[cue.SequenceIndex % candidates.size()];
            ++cue.SequenceIndex;
            cue.LastVariant = selected;
            return selected;
        }
        if (cue.Definition.Selection == EAudioSelectionMode::RandomNoRepeat && candidates.size() > 1)
        {
            candidates.erase(std::remove(candidates.begin(), candidates.end(), cue.LastVariant), candidates.end());
        }
        float totalWeight = 0.0f;
        for (int32 index : candidates) { totalWeight += std::max(0.0f, cue.Definition.Variants[static_cast<size_t>(index)].Weight); }
        int32 selected = candidates.front();
        if (totalWeight > 0.0f)
        {
            std::uniform_real_distribution<float> distribution(0.0f, totalWeight);
            float value = distribution(Random);
            for (int32 index : candidates)
            {
                value -= std::max(0.0f, cue.Definition.Variants[static_cast<size_t>(index)].Weight);
                if (value <= 0.0f) { selected = index; break; }
            }
        }
        cue.LastVariant = selected;
        return selected;
    }

    bool TriggerCue(FPlayback& playback, FCueEntry& cue)
    {
        const int32 variantIndex = SelectVariant(cue, playback.Context.GameTimeHours);
        if (variantIndex < 0) { return false; }
        const FAudioCueVariant& variant = cue.Definition.Variants[static_cast<size_t>(variantIndex)];
        playback.NextTriggerSeconds = -1.0;
        playback.FinishAfterDelay = false;
        if (variant.SilenceSeconds > 0.0f)
        {
            playback.NextTriggerSeconds = ClockSeconds + std::max(0.001f, variant.SilenceSeconds);
            playback.FinishAfterDelay = !playback.RepeatVariants;
            return true;
        }
        std::shared_ptr<FDecodedAsset> asset = LoadDecodedAsset(variant.Asset, cue.Definition.CacheLifetimeSeconds, cue.Definition.Spatialized);
        if (!asset) { return false; }
        auto source = CreateBufferedSource(playback, cue.Definition, std::move(asset));
        if (!source) { return false; }
        const double duration = source->NominalDurationSeconds;
        const uint64 sourceId = source->Id;
        playback.SourceIds.push_back(sourceId);
        ActiveSources.push_back(std::move(source));
        if (playback.RepeatVariants && cue.Definition.MixDuration > 0.0f) { playback.NextTriggerSeconds = ClockSeconds + std::max(0.01, duration - cue.Definition.MixDuration); }
        return true;
    }

    std::unique_ptr<FSource> CreateBufferedSource(const FPlayback& playback, const FAudioCueDefinition& cue, std::shared_ptr<FDecodedAsset> asset)
    {
        auto source = std::make_unique<FSource>();
        source->Id = NextSourceId++;
        source->PlaybackHandle = playback.Handle;
        source->Bus = playback.Bus;
        source->DecodedAsset = std::move(asset);
        source->BaseVolume = std::max(0.0f, cue.Volume * playback.Context.Volume);
        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_s16, source->DecodedAsset->Channels, source->DecodedAsset->FrameCount, source->DecodedAsset->Samples.data(), nullptr);
        bufferConfig.sampleRate = source->DecodedAsset->SampleRate;
        const ma_result bufferResult = ma_audio_buffer_init(&bufferConfig, &source->Buffer);
        if (bufferResult != MA_SUCCESS) { LogFailure("audio buffer initialization", bufferResult, source->DecodedAsset->LogicalName); return {}; }
        source->BufferInitialized = true;
        if (!InitializeSound(*source, cue.Spatialized, playback.Bus, source->DecodedAsset->LogicalName)) { DestroySource(*source); return {}; }
        source->FollowOffset = cue.PositionOffset;
        if (cue.RandomizeInRegion && cue.RegionRadius > 0.0f)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const float angle = unit(Random) * FullCircle;
            const float radius = std::sqrt(unit(Random)) * cue.RegionRadius;
            source->FollowOffset.X += std::sin(angle) * radius;
            source->FollowOffset.Z += std::cos(angle) * radius;
        }
        const FAudioVector3 position = playback.Context.Position + source->FollowOffset;
        const float pitch = std::max(0.01f, cue.Pitch * playback.Context.Pitch);
        source->NominalDurationSeconds = static_cast<double>(source->DecodedAsset->FrameCount) / source->DecodedAsset->SampleRate / pitch;
        ma_sound_set_looping(&source->Sound, cue.Loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(&source->Sound, source->BaseVolume);
        ma_sound_set_pitch(&source->Sound, pitch);
        if (cue.Spatialized)
        {
            ma_sound_set_position(&source->Sound, position.X, position.Y, position.Z);
            ma_sound_set_velocity(&source->Sound, playback.Context.Velocity.X, playback.Context.Velocity.Y, playback.Context.Velocity.Z);
            ma_sound_set_min_distance(&source->Sound, cue.MinDistance);
            ma_sound_set_max_distance(&source->Sound, cue.MaxDistance);
            ma_sound_set_rolloff(&source->Sound, std::max(0.0f, cue.Rolloff));
            ma_sound_set_doppler_factor(&source->Sound, std::max(0.0f, cue.DopplerFactor));
        }
        const ma_result startResult = ma_sound_start(&source->Sound);
        if (startResult != MA_SUCCESS) { LogFailure("audio source start", startResult, source->DecodedAsset->LogicalName); DestroySource(*source); return {}; }
        return source;
    }

    bool InitializeSound(FSource& source, bool spatialized, EAudioBus bus, std::string_view resourceName)
    {
        const ma_uint32 flags = spatialized ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_sound_group* group = &Groups[static_cast<size_t>(bus)];
        ma_data_source* dataSource = source.DecoderInitialized ? reinterpret_cast<ma_data_source*>(&source.Decoder) : reinterpret_cast<ma_data_source*>(&source.Buffer);
        const ma_result result = ma_sound_init_from_data_source(&Engine, dataSource, flags, group, &source.Sound);
        if (result != MA_SUCCESS) { LogFailure("audio source initialization", result, resourceName); return false; }
        source.SoundInitialized = true;
        source.Spatialized = spatialized;
        return true;
    }

    std::shared_ptr<FDecodedAsset> LoadDecodedAsset(std::string_view logicalName, float cacheLifetime, bool forceMono)
    {
        const std::string resourceKey = NormalizeAudioName(logicalName);
        const std::string key = resourceKey + (forceMono ? "|mono" : "|native");
        if (const auto cached = DecodedCache.find(key); cached != DecodedCache.end())
        {
            cached->second.LastUsedSeconds = ClockSeconds;
            cached->second.LifetimeSeconds = std::max(cached->second.LifetimeSeconds, cacheLifetime);
            return cached->second.Asset;
        }
        std::shared_ptr<FEncodedAsset> encoded = LoadEncodedAsset(logicalName);
        if (!encoded) { return {}; }
        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_s16, forceMono ? 1 : 0, 0);
        const ma_result initResult = ma_decoder_init_memory(encoded->Bytes.data(), encoded->Bytes.size(), &config, &decoder);
        if (initResult != MA_SUCCESS) { LogFailure("audio decode initialization", initResult, logicalName); return {}; }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0;
        ma_uint32 sampleRate = 0;
        const ma_result formatResult = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0);
        if (formatResult != MA_SUCCESS || format != ma_format_s16 || channels == 0 || channels > 8 || sampleRate == 0)
        {
            ma_decoder_uninit(&decoder);
            LogMissing("audio format is unsupported: " + std::string(logicalName));
            return {};
        }
        auto asset = std::make_shared<FDecodedAsset>();
        asset->LogicalName = std::string(logicalName);
        asset->Channels = channels;
        asset->SampleRate = sampleRate;
        std::vector<int16> chunk(static_cast<size_t>(DecodeChunkFrames) * channels);
        for (;;)
        {
            ma_uint64 framesRead = 0;
            const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, chunk.data(), DecodeChunkFrames, &framesRead);
            if (framesRead > 0)
            {
                const size_t samplesRead = static_cast<size_t>(framesRead) * channels;
                if (asset->Samples.size() > MaxDecodedAudioSamples - samplesRead)
                {
                    ma_decoder_uninit(&decoder);
                    LogMissing("decoded audio exceeds memory limit: " + std::string(logicalName));
                    return {};
                }
                asset->Samples.insert(asset->Samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(samplesRead));
                asset->FrameCount += framesRead;
            }
            if (readResult == MA_AT_END || framesRead == 0) { break; }
            if (readResult != MA_SUCCESS) { ma_decoder_uninit(&decoder); LogFailure("audio decode", readResult, logicalName); return {}; }
        }
        ma_decoder_uninit(&decoder);
        if (asset->FrameCount == 0) { LogMissing("decoded audio is empty: " + std::string(logicalName)); return {}; }
        DecodedCache[key] = FCachedDecodedAsset{asset, ClockSeconds, std::max(0.0f, cacheLifetime)};
        return asset;
    }

    std::shared_ptr<FEncodedAsset> LoadEncodedAsset(std::string_view logicalName)
    {
        const std::string key = NormalizeAudioName(logicalName);
        if (const auto cached = EncodedCache.find(key); cached != EncodedCache.end()) { return cached->second; }
        TResult<FResourceBlob> loaded = Resources->Load(logicalName);
        if (!loaded.IsOk()) { LogMissing(loaded.Status().Message()); return {}; }
        FResourceBlob& blob = loaded.Value();
        if (blob.Bytes.empty()) { LogMissing("audio resource is empty: " + std::string(logicalName)); return {}; }
        if (blob.Bytes.size() > MaxEncodedAudioBytes) { LogMissing("audio resource exceeds memory limit: " + std::string(logicalName)); return {}; }
        auto asset = std::make_shared<FEncodedAsset>();
        asset->LogicalName = std::string(logicalName);
        asset->Bytes = std::move(blob.Bytes);
        EncodedCache[key] = asset;
        return asset;
    }

    std::unique_ptr<FSource> CreateStreamingSource(const std::shared_ptr<FEncodedAsset>& asset, float volume)
    {
        auto source = std::make_unique<FSource>();
        source->Id = NextSourceId++;
        source->Bus = EAudioBus::Music;
        source->EncodedAsset = asset;
        source->BaseVolume = std::max(0.0f, volume);
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        const ma_result decoderResult = ma_decoder_init_memory(asset->Bytes.data(), asset->Bytes.size(), &config, &source->Decoder);
        if (decoderResult != MA_SUCCESS) { LogFailure("music decoder initialization", decoderResult, asset->LogicalName); return {}; }
        source->DecoderInitialized = true;
        if (!InitializeSound(*source, false, EAudioBus::Music, asset->LogicalName)) { DestroySource(*source); return {}; }
        ma_sound_set_volume(&source->Sound, source->BaseVolume);
        ma_sound_set_looping(&source->Sound, MA_FALSE);
        return source;
    }

    void UpdateSources(float delta)
    {
        for (auto& source : ActiveSources) { UpdateSourceFade(*source, delta); }
        for (auto& music : MusicStates) { if (music->Source) { UpdateSourceFade(*music->Source, delta); } }
        auto it = ActiveSources.begin();
        while (it != ActiveSources.end())
        {
            FSource& source = **it;
            const bool fadedOut = source.StopWhenSilent && source.FadeGain <= 0.0f && source.FadeSpeed <= 0.0f;
            const bool ended = source.SoundInitialized && ma_sound_at_end(&source.Sound) == MA_TRUE;
            if (!fadedOut && !ended) { ++it; continue; }
            if (FPlayback* playback = FindPlayback(source.PlaybackHandle))
            {
                playback->SourceIds.erase(std::remove(playback->SourceIds.begin(), playback->SourceIds.end(), source.Id), playback->SourceIds.end());
            }
            DestroySource(source);
            it = ActiveSources.erase(it);
        }
    }

    void UpdateSourceFade(FSource& source, float delta)
    {
        if (!source.SoundInitialized || source.FadeSpeed <= 0.0f) { return; }
        const float direction = source.FadeTarget >= source.FadeGain ? 1.0f : -1.0f;
        source.FadeGain += direction * source.FadeSpeed * delta;
        if ((direction > 0.0f && source.FadeGain >= source.FadeTarget) || (direction < 0.0f && source.FadeGain <= source.FadeTarget))
        {
            source.FadeGain = source.FadeTarget;
            source.FadeSpeed = 0.0f;
        }
        ma_sound_set_volume(&source.Sound, source.BaseVolume * source.FadeGain);
    }

    void UpdatePlaybacks()
    {
        std::vector<FAudioHandle> finished;
        for (auto& playback : Playbacks)
        {
            if (playback->StopRequested)
            {
                if (playback->SourceIds.empty()) { finished.push_back(playback->Handle); }
                continue;
            }
            auto cueIt = Cues.find(playback->CueName);
            if (cueIt == Cues.end()) { finished.push_back(playback->Handle); continue; }
            if (playback->NextTriggerSeconds >= 0.0 && ClockSeconds >= playback->NextTriggerSeconds)
            {
                if (playback->FinishAfterDelay) { finished.push_back(playback->Handle); continue; }
                if (!TriggerCue(*playback, cueIt->second)) { finished.push_back(playback->Handle); }
                continue;
            }
            if (playback->SourceIds.empty() && playback->NextTriggerSeconds < 0.0)
            {
                if (playback->RepeatVariants)
                {
                    if (!TriggerCue(*playback, cueIt->second)) { finished.push_back(playback->Handle); }
                }
                else { finished.push_back(playback->Handle); }
            }
        }
        for (FAudioHandle handle : finished) { RemovePlayback(handle); }
    }

    void UpdateMusic(float delta)
    {
        for (auto& music : MusicStates)
        {
            UpdateMusicFade(*music, delta);
            if (music->Finished) { continue; }
            if (music->WaitUntilSeconds >= 0.0 && ClockSeconds >= music->WaitUntilSeconds)
            {
                music->WaitUntilSeconds = -1.0;
                ExecuteMusic(*music);
            }
            if (music->WholeTrack && music->Source && music->Source->SoundInitialized)
            {
                if (music->Definition.AutoFree && ma_sound_at_end(&music->Source->Sound) == MA_TRUE) { music->Finished = true; }
                continue;
            }
            if (music->PlayingSample && music->Source && music->Source->SoundInitialized)
            {
                float cursor = 0.0f;
                const bool cursorKnown = ma_sound_get_cursor_in_seconds(&music->Source->Sound, &cursor) == MA_SUCCESS;
                if ((cursorKnown && cursor >= music->SampleEndSeconds) || ma_sound_at_end(&music->Source->Sound) == MA_TRUE)
                {
                    ma_sound_stop(&music->Source->Sound);
                    music->PlayingSample = false;
                    ExecuteMusic(*music);
                }
            }
        }
        auto it = MusicStates.begin();
        while (it != MusicStates.end())
        {
            FMusicRuntime& music = **it;
            const bool silent = music.StopWhenSilent && music.FadeGain <= 0.0f && music.FadeSpeed <= 0.0f;
            if (!music.Finished && !silent) { ++it; continue; }
            if (music.Source) { DestroySource(*music.Source); }
            it = MusicStates.erase(it);
        }
    }

    void ExecuteMusic(FMusicRuntime& music)
    {
        for (int32 guard = 0; guard < 64 && !music.Finished && !music.PlayingSample && music.WaitUntilSeconds < 0.0; ++guard)
        {
            const FMusicPatternDefinition* pattern = FindPattern(music.Definition, music.PatternIndex);
            if (!pattern || pattern->Commands.empty()) { music.Finished = true; break; }
            if (music.CommandIndex >= pattern->Commands.size()) { music.CommandIndex = 0; }
            const FMusicPatternCommand command = pattern->Commands[music.CommandIndex++];
            if (command.Type == EMusicPatternCommandType::PlaySample)
            {
                const FMusicSampleRange* sample = FindSample(music.Definition, command.Value);
                if (!sample) { continue; }
                if (!music.Source)
                {
                    music.Source = CreateStreamingSource(music.Asset, music.Definition.Volume * music.RequestVolume * music.FadeGain);
                    if (!music.Source) { music.Finished = true; break; }
                }
                ma_sound_stop(&music.Source->Sound);
                if (ma_sound_seek_to_second(&music.Source->Sound, static_cast<float>(sample->StartSeconds)) != MA_SUCCESS) { music.Finished = true; break; }
                music.SampleEndSeconds = sample->EndSeconds;
                ApplyMusicVolume(music);
                if (ma_sound_start(&music.Source->Sound) != MA_SUCCESS) { music.Finished = true; break; }
                music.PlayingSample = true;
            }
            else if (command.Type == EMusicPatternCommandType::Wait) { music.WaitUntilSeconds = ClockSeconds + std::max(0, command.Value) / 1000.0; }
            else if (command.Type == EMusicPatternCommandType::Jump) { music.PatternIndex = command.Value; music.CommandIndex = 0; }
            else { music.Finished = true; }
        }
    }

    const FMusicPatternDefinition* FindPattern(const FMusicTrackDefinition& track, int32 index) const
    {
        const auto it = std::find_if(track.Patterns.begin(), track.Patterns.end(), [index](const FMusicPatternDefinition& pattern) { return pattern.Index == index; });
        return it != track.Patterns.end() ? &*it : nullptr;
    }

    const FMusicSampleRange* FindSample(const FMusicTrackDefinition& track, int32 index) const
    {
        const auto it = std::find_if(track.Samples.begin(), track.Samples.end(), [index](const FMusicSampleRange& sample) { return sample.Index == index; });
        return it != track.Samples.end() ? &*it : nullptr;
    }

    void BeginMusicFadeOut(float fadeSeconds)
    {
        for (auto& music : MusicStates)
        {
            music->FadeTarget = 0.0f;
            music->FadeSpeed = fadeSeconds > 0.0f ? std::abs(music->FadeGain) / fadeSeconds : 0.0f;
            music->StopWhenSilent = true;
            if (fadeSeconds <= 0.0f) { music->FadeGain = 0.0f; music->Finished = true; }
        }
    }

    void UpdateMusicFade(FMusicRuntime& music, float delta)
    {
        if (music.FadeSpeed <= 0.0f) { return; }
        const float direction = music.FadeTarget >= music.FadeGain ? 1.0f : -1.0f;
        music.FadeGain += direction * music.FadeSpeed * delta;
        if ((direction > 0.0f && music.FadeGain >= music.FadeTarget) || (direction < 0.0f && music.FadeGain <= music.FadeTarget))
        {
            music.FadeGain = music.FadeTarget;
            music.FadeSpeed = 0.0f;
        }
        ApplyMusicVolume(music);
    }

    void ApplyMusicVolume(FMusicRuntime& music)
    {
        if (!music.Source || !music.Source->SoundInitialized) { return; }
        music.Source->BaseVolume = music.Definition.Volume * music.RequestVolume;
        ma_sound_set_volume(&music.Source->Sound, music.Source->BaseVolume * music.FadeGain);
    }

    void BeginFade(FSource& source, float target, float seconds, bool stopWhenSilent)
    {
        source.FadeTarget = std::clamp(target, 0.0f, 1.0f);
        source.StopWhenSilent = stopWhenSilent;
        if (seconds <= 0.0f)
        {
            source.FadeGain = source.FadeTarget;
            source.FadeSpeed = 0.0f;
            if (source.SoundInitialized) { ma_sound_set_volume(&source.Sound, source.BaseVolume * source.FadeGain); }
            return;
        }
        source.FadeSpeed = std::abs(source.FadeTarget - source.FadeGain) / seconds;
    }

    void DestroySource(FSource& source)
    {
        if (source.SoundInitialized) { ma_sound_uninit(&source.Sound); source.SoundInitialized = false; }
        if (source.DecoderInitialized) { ma_decoder_uninit(&source.Decoder); source.DecoderInitialized = false; }
        if (source.BufferInitialized) { ma_audio_buffer_uninit(&source.Buffer); source.BufferInitialized = false; }
    }

    FSource* FindSource(uint64 sourceId)
    {
        const auto it = std::find_if(ActiveSources.begin(), ActiveSources.end(), [sourceId](const std::unique_ptr<FSource>& source) { return source->Id == sourceId; });
        return it != ActiveSources.end() ? it->get() : nullptr;
    }

    FPlayback* FindPlayback(FAudioHandle handle)
    {
        const auto it = std::find_if(Playbacks.begin(), Playbacks.end(), [handle](const std::unique_ptr<FPlayback>& playback) { return playback->Handle == handle; });
        return it != Playbacks.end() ? it->get() : nullptr;
    }

    void RemovePlayback(FAudioHandle handle)
    {
        FPlayback* playback = FindPlayback(handle);
        if (!playback) { return; }
        const std::vector<uint64> sourceIds = playback->SourceIds;
        for (uint64 sourceId : sourceIds)
        {
            const auto sourceIt = std::find_if(ActiveSources.begin(), ActiveSources.end(), [sourceId](const std::unique_ptr<FSource>& source) { return source->Id == sourceId; });
            if (sourceIt != ActiveSources.end()) { DestroySource(**sourceIt); ActiveSources.erase(sourceIt); }
        }
        Playbacks.erase(std::remove_if(Playbacks.begin(), Playbacks.end(), [handle](const std::unique_ptr<FPlayback>& item) { return item->Handle == handle; }), Playbacks.end());
    }

    int32 CountCueInstances(std::string_view cueName) const
    {
        return static_cast<int32>(std::count_if(Playbacks.begin(), Playbacks.end(), [cueName](const std::unique_ptr<FPlayback>& playback) { return playback->CueName == cueName; }));
    }

    void RemoveOldestPlayback(std::string_view cueName)
    {
        FPlayback* oldest = nullptr;
        for (const auto& playback : Playbacks)
        {
            if (playback->CueName != cueName) { continue; }
            if (!oldest || playback->CreatedSeconds < oldest->CreatedSeconds) { oldest = playback.get(); }
        }
        if (oldest) { RemovePlayback(oldest->Handle); }
    }

    void ExpireCache()
    {
        for (auto it = DecodedCache.begin(); it != DecodedCache.end();)
        {
            const bool expired = it->second.LifetimeSeconds <= 0.0f || ClockSeconds - it->second.LastUsedSeconds >= it->second.LifetimeSeconds;
            if (expired && it->second.Asset.use_count() == 1) { it = DecodedCache.erase(it); }
            else { ++it; }
        }
        for (auto it = EncodedCache.begin(); it != EncodedCache.end();)
        {
            if (it->second.use_count() == 1 && DecodedCache.find(it->first) == DecodedCache.end()) { it = EncodedCache.erase(it); }
            else { ++it; }
        }
    }

    void LogFailure(std::string_view action, ma_result result, std::string_view resource)
    {
        LogMissing(std::string(action) + " failed for " + std::string(resource) + ": " + std::to_string(result));
    }

    void LogMissing(std::string message)
    {
        if (!MissingMessages.insert(message).second) { return; }
        if (Log) { Log->Warning(std::move(message)); }
    }

    mutable std::mutex Mutex;
    const FResourceManager* Resources = nullptr;
    FLogger* Log = nullptr;
    ma_engine Engine{};
    std::array<ma_sound_group, AudioBusCount> Groups{};
    size_t InitializedGroupCount = 0;
    bool EngineInitialized = false;
    bool Initialized = false;
    uint64 NextHandle = 1;
    uint64 NextSourceId = 1;
    double ClockSeconds = 0.0;
    float GameTimeHours = -1.0f;
    std::mt19937 Random{std::random_device{}()};
    std::unordered_map<std::string, FCueEntry> Cues;
    std::unordered_map<std::string, FMusicTrackDefinition> MusicTracks;
    std::unordered_map<int32, std::string> LegacyEffects;
    std::unordered_map<std::string, FCachedDecodedAsset> DecodedCache;
    std::unordered_map<std::string, std::shared_ptr<FEncodedAsset>> EncodedCache;
    std::vector<std::unique_ptr<FPlayback>> Playbacks;
    std::vector<std::unique_ptr<FSource>> ActiveSources;
    std::vector<std::unique_ptr<FMusicRuntime>> MusicStates;
    std::string CurrentMusicName;
    std::unordered_set<std::string> MissingMessages;
};

FAudioSystem::FAudioSystem() : Impl(std::make_unique<FImpl>()) {}
FAudioSystem::~FAudioSystem() { Shutdown(); }

FStatus FAudioSystem::Initialize(const FResourceManager& resources, FLogger* logger)
{
    FStatus status = Impl->Initialize(resources, logger);
    if (!status.IsOk()) { return status; }
    Impl->RegisterDefaults();
    Impl->ImportLegacyDefinitions(*this);
    return FStatus::Ok();
}

void FAudioSystem::Shutdown() { Impl->Shutdown(); }
bool FAudioSystem::IsInitialized() const { return Impl->IsInitialized(); }
void FAudioSystem::RegisterCue(std::string name, FAudioCueDefinition definition) { Impl->RegisterCue(std::move(name), std::move(definition)); }
void FAudioSystem::RegisterMusicTrack(std::string name, FMusicTrackDefinition definition) { Impl->RegisterMusicTrack(std::move(name), std::move(definition)); }
bool FAudioSystem::RegisterMusicTrackIfMissing(std::string name, FMusicTrackDefinition definition) { return Impl->RegisterMusicTrackIfMissing(std::move(name), std::move(definition)); }
void FAudioSystem::RegisterLegacyEffect(int32 effectId, std::string cueName) { Impl->RegisterLegacyEffect(effectId, std::move(cueName)); }
FAudioImportReport FAudioSystem::ImportLegacyDefinitions() { return Impl->ImportLegacyDefinitions(*this); }
FAudioHandle FAudioSystem::PlayEvent(EAudioEvent event) { return PlayCue(EventCueName(event)); }
FAudioHandle FAudioSystem::PlayCue(std::string_view cueName, const FAudioPlayContext& context) { return Impl->PlayCue(cueName, context); }
FAudioHandle FAudioSystem::PlayLegacyEffect(int32 effectId, const FAudioPlayContext& context) { return Impl->PlayLegacyEffect(effectId, context); }

FAudioHandle FAudioSystem::PlayCreatureSound(int32 soundBase, ELegacyCreatureCue cue, const FAudioPlayContext& context)
{
    FAudioPlayContext routed = context;
    routed.BusOverride = EAudioBus::Creatures;
    return PlayLegacyEffect(FLegacyAudioResolver::CreatureEffectId(soundBase, cue), routed);
}

FAudioHandle FAudioSystem::PlayFootstep(int32 stepBase, int32 rawGroundCode, ELegacyFootstepCue cue, const FAudioPlayContext& context)
{
    FAudioPlayContext routed = context;
    routed.BusOverride = EAudioBus::Creatures;
    return PlayLegacyEffect(FLegacyAudioResolver::FootstepEffectId(stepBase, rawGroundCode, cue), routed);
}

FAudioHandle FAudioSystem::PlayWeapon(ELegacyWeaponCue cue, const FAudioPlayContext& context)
{
    FAudioPlayContext routed = context;
    routed.BusOverride = EAudioBus::Effects;
    return PlayLegacyEffect(static_cast<int32>(cue), routed);
}

bool FAudioSystem::Stop(FAudioHandle handle, float fadeSeconds) { return Impl->Stop(handle, fadeSeconds); }
void FAudioSystem::StopBus(EAudioBus bus, float fadeSeconds) { Impl->StopBus(bus, fadeSeconds); }
bool FAudioSystem::SetSourceTransform(FAudioHandle handle, const FAudioVector3& position, const FAudioVector3& velocity) { return Impl->SetSourceTransform(handle, position, velocity); }
void FAudioSystem::PlayMusic(const FMusicRequest& request) { Impl->PlayMusic(request); }
void FAudioSystem::StopMusic(float fadeSeconds) { Impl->StopMusic(fadeSeconds); }
void FAudioSystem::SetListener(const FAudioListenerState& listener) { Impl->SetListener(listener); }
void FAudioSystem::SetGameTimeHours(float hour) { Impl->SetGameTimeHours(hour); }
void FAudioSystem::SetMasterVolume(float volume) { Impl->SetMasterVolume(volume); }
void FAudioSystem::SetBusVolume(EAudioBus bus, float volume) { Impl->SetBusVolume(bus, volume); }
void FAudioSystem::ClearCache() { Impl->ClearCache(); }
void FAudioSystem::Update(float deltaSeconds) { Impl->Update(deltaSeconds); }
