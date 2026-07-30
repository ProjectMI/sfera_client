#include "Components/Audio/LegacyAudioDefinitionLoader.h"
#include "Components/Audio/AudioSystem.h"
#include "Components/Audio/LegacyAudioResolver.h"
#include "Core/Logger.h"
#include "ResourceLoader/ResourceManager.h"

namespace
{
    struct FDefinitionBlock
    {
        std::string Type;
        std::string Name;
        std::vector<std::string> Lines;
    };

    struct FTimeGroup
    {
        float Start = -1.0f;
        float End = -1.0f;
        int32 First = 0;
        int32 Last = 0;
    };

    std::string Lower(std::string value)
    {
        for (char& c : value) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return value;
    }

    std::vector<std::string> Tokenize(std::string_view line)
    {
        std::vector<std::string> tokens;
        std::string token;
        bool quoted = false;
        char quote = 0;
        auto flush = [&]() { if (!token.empty()) { tokens.push_back(std::move(token)); token.clear(); } };
        for (size_t index = 0; index < line.size(); ++index)
        {
            const char c = line[index];
            if (!quoted && c == '/' && index + 1 < line.size() && line[index + 1] == '/') { break; }
            if (quoted)
            {
                if (c == quote) { flush(); quoted = false; }
                else { token.push_back(c); }
                continue;
            }
            if (c == '"' || c == '\'') { flush(); quoted = true; quote = c; continue; }
            if (std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == ',' || c == ';' || c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']' || c == '|') { flush(); continue; }
            token.push_back(c);
        }
        flush();
        return tokens;
    }

    bool ParseInt(std::string_view token, int32& value)
    {
        const char* first = token.data();
        const char* last = token.data() + token.size();
        int64 parsed = 0;
        int base = 10;
        if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) { first += 2; base = 16; }
        const auto result = std::from_chars(first, last, parsed, base);
        if (result.ec != std::errc{} || result.ptr != last || parsed < std::numeric_limits<int32>::min() || parsed > std::numeric_limits<int32>::max()) { return false; }
        value = static_cast<int32>(parsed);
        return true;
    }

    bool ParseFloat(std::string_view token, float& value)
    {
        std::string text(token);
        char* end = nullptr;
        value = std::strtof(text.c_str(), &end);
        return end && *end == 0 && end != text.c_str() && std::isfinite(value);
    }

    float ParseTimestamp(std::string_view token)
    {
        const size_t colon = token.find(':');
        if (colon == std::string_view::npos)
        {
            float value = -1.0f;
            return ParseFloat(token, value) ? value : -1.0f;
        }
        float minutes = 0.0f;
        float seconds = 0.0f;
        if (!ParseFloat(token.substr(0, colon), minutes) || !ParseFloat(token.substr(colon + 1), seconds)) { return -1.0f; }
        return minutes * 60.0f + seconds;
    }

    float ParseHour(std::string_view token)
    {
        const size_t colon = token.find(':');
        if (colon == std::string_view::npos)
        {
            float value = -1.0f;
            return ParseFloat(token, value) ? value : -1.0f;
        }
        float hours = 0.0f;
        float minutes = 0.0f;
        if (!ParseFloat(token.substr(0, colon), hours) || !ParseFloat(token.substr(colon + 1), minutes)) { return -1.0f; }
        return hours + minutes / 60.0f;
    }

    bool IsAudioAsset(std::string_view token)
    {
        const std::string lower = Lower(std::string(token));
        return lower.ends_with(".wav") || lower.ends_with(".ogg") || lower.ends_with(".mp3") || lower.ends_with(".flac");
    }

    bool IsMusicPath(std::string_view logicalName)
    {
        const std::string lower = Lower(std::string(logicalName));
        return lower.starts_with("sounds/music/") || lower.starts_with("sounds\\music\\");
    }

    std::string ResolveAudioAsset(const FResourceManager& resources, const FFileRecord& definitionRecord, std::string asset)
    {
        for (char& c : asset) { if (c == '\\') { c = '/'; } }
        std::array<std::string, 3> candidates{asset, (definitionRecord.RelativePath.parent_path() / asset).generic_string(), (FPath("sounds/music") / asset).generic_string()};
        for (const std::string& candidate : candidates)
        {
            if (const auto record = resources.Catalog().FindByLogicalName(candidate)) { return record->RelativePath.generic_string(); }
        }
        return asset;
    }

    std::vector<std::string> MusicAliases(std::string_view logicalName)
    {
        std::string relative(logicalName);
        for (char& c : relative) { if (c == '\\') { c = '/'; } }
        const std::string lower = Lower(relative);
        constexpr std::string_view prefix = "sounds/music/";
        if (lower.starts_with(prefix)) { relative.erase(0, prefix.size()); }
        FPath path(relative);
        path.replace_extension();
        std::vector<std::string> aliases;
        const auto append = [&aliases](std::string value)
        {
            if (!value.empty() && std::find(aliases.begin(), aliases.end(), value) == aliases.end()) { aliases.push_back(std::move(value)); }
        };
        append(path.filename().generic_string());
        append(path.generic_string());
        if (path.has_parent_path() && path.parent_path() != ".") { append(path.parent_path().filename().generic_string()); }
        return aliases;
    }

    std::vector<FDefinitionBlock> ExtractBlocks(std::string_view text, std::initializer_list<std::string_view> acceptedTypes)
    {
        std::vector<FDefinitionBlock> blocks;
        std::istringstream stream{std::string(text)};
        std::string line;
        FDefinitionBlock current;
        int32 depth = 0;
        bool active = false;
        while (std::getline(stream, line))
        {
            const std::vector<std::string> tokens = Tokenize(line);
            if (!active)
            {
                if (tokens.empty()) { continue; }
                const std::string type = Lower(tokens.front());
                const bool accepted = std::any_of(acceptedTypes.begin(), acceptedTypes.end(), [&type](std::string_view candidate) { return type == candidate; });
                if (!accepted) { continue; }
                current = {};
                current.Type = type;
                if (tokens.size() > 1) { current.Name = tokens[1]; }
                active = true;
            }
            current.Lines.push_back(line);
            depth += static_cast<int32>(std::count(line.begin(), line.end(), '{'));
            depth -= static_cast<int32>(std::count(line.begin(), line.end(), '}'));
            if (active && depth <= 0 && line.find('}') != std::string::npos)
            {
                blocks.push_back(std::move(current));
                current = {};
                active = false;
                depth = 0;
            }
        }
        if (active && !current.Lines.empty()) { blocks.push_back(std::move(current)); }
        return blocks;
    }

    void ApplyTimeGroups(std::vector<FAudioCueVariant>& variants, const std::vector<FTimeGroup>& groups)
    {
        for (const FTimeGroup& group : groups)
        {
            if (variants.empty()) { break; }
            const int32 first = std::clamp(group.First, 0, static_cast<int32>(variants.size()) - 1);
            const int32 last = std::clamp(group.Last, first, static_cast<int32>(variants.size()) - 1);
            for (int32 index = first; index <= last; ++index)
            {
                variants[static_cast<size_t>(index)].StartHour = group.Start;
                variants[static_cast<size_t>(index)].EndHour = group.End;
            }
        }
    }

    bool ParseSoundEffect(const FDefinitionBlock& block, int32& effectId, FAudioCueDefinition& cue, bool requireEffectId)
    {
        constexpr int32 Environment = 0x01;
        constexpr int32 Random = 0x04;
        constexpr int32 RandomMix = 0x08;
        constexpr int32 Looped = 0x10;
        constexpr int32 UseRegion = 0x20;
        constexpr int32 TimeGroups = 0x40;
        int32 flags = 0;
        std::vector<FTimeGroup> groups;
        cue.Spatialized = true;
        for (const std::string& line : block.Lines)
        {
            std::vector<std::string> tokens = Tokenize(line);
            if (tokens.empty()) { continue; }
            for (std::string& token : tokens) { token = Lower(std::move(token)); }
            const std::string& key = tokens.front();
            if ((key == "soundeffect" || key == "eff_number" || key == "effect_number") && tokens.size() > 1)
            {
                for (size_t index = 1; index < tokens.size(); ++index) { if (ParseInt(tokens[index], effectId)) { break; } }
            }
            else if (key == "audio_file" || key == "audio_files" || key == "source")
            {
                const auto appendSource = [&cue](const std::vector<std::string>& values, size_t first)
                {
                    const auto silence = std::find(values.begin() + static_cast<std::ptrdiff_t>(first), values.end(), "silence");
                    if (silence != values.end())
                    {
                        FAudioCueVariant variant;
                        for (auto it = std::next(silence); it != values.end(); ++it)
                        {
                            float duration = 0.0f;
                            if (ParseFloat(*it, duration) && duration >= 0.0f) { variant.SilenceSeconds = duration; break; }
                        }
                        if (variant.SilenceSeconds > 0.0f) { cue.Variants.push_back(std::move(variant)); }
                        return;
                    }
                    for (size_t index = first; index < values.size(); ++index)
                    {
                        if (IsAudioAsset(values[index])) { cue.Variants.push_back(FAudioCueVariant{values[index]}); }
                    }
                };
                appendSource(tokens, 1);
            }
            else if (key == "flags")
            {
                for (size_t index = 1; index < tokens.size(); ++index)
                {
                    int32 numeric = 0;
                    if (ParseInt(tokens[index], numeric)) { flags |= numeric; }
                    else if (tokens[index] == "sf_type_environment" || tokens[index] == "environment") { flags |= Environment; }
                    else if (tokens[index] == "sf_play_random" || tokens[index] == "random") { flags |= Random; }
                    else if (tokens[index] == "sf_play_randommix" || tokens[index] == "randommix") { flags |= RandomMix; }
                    else if (tokens[index] == "sf_play_looped" || tokens[index] == "looped") { flags |= Looped; }
                    else if (tokens[index] == "sf_play_useregion" || tokens[index] == "useregion") { flags |= UseRegion; }
                    else if (tokens[index] == "sf_play_timegroups" || tokens[index] == "timegroups") { flags |= TimeGroups; }
                }
            }
            else if ((key == "time" || key == "time_groups") && tokens.size() >= 5)
            {
                const auto timeIt = std::find(tokens.begin(), tokens.end(), "time");
                const size_t first = timeIt != tokens.end() ? static_cast<size_t>(std::distance(tokens.begin(), timeIt)) + 1 : 1;
                if (tokens.size() >= first + 4)
                {
                    FTimeGroup group;
                    group.Start = ParseHour(tokens[first]);
                    group.End = ParseHour(tokens[first + 1]);
                    ParseInt(tokens[first + 2], group.First);
                    ParseInt(tokens[first + 3], group.Last);
                    if (group.Start >= 0.0f && group.End >= 0.0f) { groups.push_back(group); }
                }
            }
            else if (key == "region_radius" && tokens.size() > 1) { ParseFloat(tokens[1], cue.RegionRadius); }
            else if (key == "min_distance" && tokens.size() > 1) { ParseFloat(tokens[1], cue.MinDistance); }
            else if (key == "max_distance" && tokens.size() > 1) { ParseFloat(tokens[1], cue.MaxDistance); }
            else if (key == "mix_duration" && tokens.size() > 1) { ParseFloat(tokens[1], cue.MixDuration); }
            else if (key == "vol_barier" && tokens.size() > 1) { ParseFloat(tokens[1], cue.VolumeBarrier); }
            else if (key == "cache_lifetime" && tokens.size() > 1) { ParseFloat(tokens[1], cue.CacheLifetimeSeconds); }
            else if (key == "volume" && tokens.size() > 1) { ParseFloat(tokens[1], cue.Volume); }
            else if (key == "pitch" && tokens.size() > 1) { ParseFloat(tokens[1], cue.Pitch); }
            else if (key == "offset_vec" && tokens.size() >= 4)
            {
                ParseFloat(tokens[1], cue.PositionOffset.X);
                ParseFloat(tokens[2], cue.PositionOffset.Y);
                ParseFloat(tokens[3], cue.PositionOffset.Z);
            }
        }
        cue.Bus = (flags & Environment) != 0 ? EAudioBus::Ambience : EAudioBus::Effects;
        cue.Spatialized = (flags & Environment) == 0;
        cue.Selection = (flags & (Random | RandomMix)) != 0 ? EAudioSelectionMode::RandomNoRepeat : cue.Variants.size() > 1 ? EAudioSelectionMode::Sequence : EAudioSelectionMode::First;
        cue.Loop = (flags & Looped) != 0;
        cue.RepeatVariants = !cue.Loop && (flags & (Random | RandomMix)) != 0;
        cue.RandomizeInRegion = (flags & UseRegion) != 0;
        if ((flags & TimeGroups) != 0) { ApplyTimeGroups(cue.Variants, groups); }
        cue.MaxDistance = std::max(cue.MinDistance, cue.MaxDistance);
        cue.CacheLifetimeSeconds = std::max(0.0f, cue.CacheLifetimeSeconds);
        if (cue.Volume > 1.0f) { cue.Volume *= 0.01f; }
        cue.Volume = std::max(0.0f, cue.Volume);
        return (!requireEffectId || effectId >= 0) && !cue.Variants.empty();
    }

    std::vector<FMusicPatternCommand> ParsePatternCommands(const std::vector<std::string>& tokens, size_t first)
    {
        std::vector<FMusicPatternCommand> commands;
        for (size_t index = first; index < tokens.size(); ++index)
        {
            const std::string token = Lower(tokens[index]);
            if (token.empty()) { continue; }
            if (token == "end" || token == "stop" || token == "e") { commands.push_back({EMusicPatternCommandType::Stop, 0}); continue; }
            int32 value = 0;
            const char prefix = token.front();
            if (!ParseInt(std::string_view(token).substr(1), value)) { continue; }
            if (prefix == 'p') { commands.push_back({EMusicPatternCommandType::PlaySample, value}); }
            else if (prefix == 's') { commands.push_back({EMusicPatternCommandType::Wait, value}); }
            else if (prefix == 'j') { commands.push_back({EMusicPatternCommandType::Jump, value}); }
        }
        return commands;
    }

    bool ParseSoundtrack(const FDefinitionBlock& block, std::string_view fallbackName, std::string& name, FMusicTrackDefinition& track)
    {
        if (!block.Name.empty()) { name = block.Name; }
        else { name = fallbackName; }
        for (const std::string& line : block.Lines)
        {
            std::vector<std::string> tokens = Tokenize(line);
            if (tokens.empty()) { continue; }
            const std::string key = Lower(tokens.front());
            if (key == "soundtrack" && tokens.size() > 1 && name.empty()) { name = tokens[1]; }
            else if (key == "audio_file" && tokens.size() > 1)
            {
                for (size_t index = 1; index < tokens.size(); ++index) { if (IsAudioAsset(tokens[index])) { track.Asset = tokens[index]; break; } }
            }
            else if (key == "volume" && tokens.size() > 1) { ParseFloat(tokens[1], track.Volume); if (track.Volume > 1.0f) { track.Volume *= 0.01f; } track.Volume = std::max(0.0f, track.Volume); }
            else if (key == "flags")
            {
                for (size_t index = 1; index < tokens.size(); ++index)
                {
                    const std::string flag = Lower(tokens[index]);
                    int32 numeric = 0;
                    if (flag == "st_autofree" || flag == "autofree") { track.AutoFree = true; }
                    else if (ParseInt(flag, numeric) && (numeric & 0x01) != 0) { track.AutoFree = true; }
                }
            }
            else if (key == "start_pattern" && tokens.size() > 1) { ParseInt(tokens[1], track.StartPattern); }
            else if (key == "sample" && tokens.size() >= 4)
            {
                FMusicSampleRange sample;
                float start = ParseTimestamp(tokens[tokens.size() - 2]);
                float end = ParseTimestamp(tokens[tokens.size() - 1]);
                ParseInt(tokens[1], sample.Index);
                sample.StartSeconds = std::max(0.0f, start);
                sample.EndSeconds = std::max(sample.StartSeconds, static_cast<double>(end));
                if (sample.EndSeconds > sample.StartSeconds) { track.Samples.push_back(sample); }
            }
            else if (key == "pattern" && tokens.size() >= 3)
            {
                FMusicPatternDefinition pattern;
                ParseInt(tokens[1], pattern.Index);
                pattern.Commands = ParsePatternCommands(tokens, 2);
                if (!pattern.Commands.empty()) { track.Patterns.push_back(std::move(pattern)); }
            }
        }
        if (track.Patterns.empty() && !track.Samples.empty())
        {
            FMusicPatternDefinition pattern;
            pattern.Index = track.StartPattern;
            for (const FMusicSampleRange& sample : track.Samples) { pattern.Commands.push_back({EMusicPatternCommandType::PlaySample, sample.Index}); }
            pattern.Commands.push_back({EMusicPatternCommandType::Jump, track.StartPattern});
            track.Patterns.push_back(std::move(pattern));
        }
        return !name.empty() && !track.Asset.empty();
    }

    bool ParseEffectNumber(const FDefinitionBlock& block, int32& effectId)
    {
        for (const std::string& line : block.Lines)
        {
            std::vector<std::string> tokens = Tokenize(line);
            if (tokens.empty() || Lower(tokens.front()) != "effect_number") { continue; }
            for (size_t index = 1; index < tokens.size(); ++index) { if (ParseInt(tokens[index], effectId)) { return true; } }
        }
        return false;
    }

    size_t ImportEmbeddedEffectSounds(std::string_view text, FAudioSystem& audio, FAudioImportReport& report)
    {
        size_t imported = 0;
        for (const FDefinitionBlock& effectBlock : ExtractBlocks(text, {"effect_def"}))
        {
            int32 effectId = -1;
            if (!ParseEffectNumber(effectBlock, effectId) || effectId < 0) { continue; }
            std::string effectText;
            for (const std::string& line : effectBlock.Lines) { effectText += line; effectText.push_back('\n'); }
            const std::vector<FDefinitionBlock> soundBlocks = ExtractBlocks(effectText, {"sound_def"});
            if (soundBlocks.empty()) { continue; }
            int32 ignoredId = -1;
            FAudioCueDefinition cue;
            if (!ParseSoundEffect(soundBlocks.front(), ignoredId, cue, false)) { ++report.DefinitionsSkipped; continue; }
            const std::string cueName = FLegacyAudioResolver::CueNameForEffect(effectId);
            audio.RegisterCue(cueName, std::move(cue));
            audio.RegisterLegacyEffect(effectId, cueName);
            ++report.ScriptedEffectsImported;
            ++imported;
        }
        return imported;
    }
}

FAudioImportReport FLegacyAudioDefinitionLoader::Import(const FResourceManager& resources, FAudioSystem& audio, FLogger* logger)
{
    FAudioImportReport report;
    for (const FFileRecord& record : resources.Catalog().All())
    {
        const std::string logicalName = record.RelativePath.generic_string();
        const std::string lower = Lower(logicalName);
        const bool definitionFile = lower.ends_with(".def");
        const bool musicDefinitionFile = IsMusicPath(logicalName) && (definitionFile || lower.ends_with(".sst"));
        const bool scriptedEffectFile = lower.ends_with(".sef") || lower.ends_with(".ssm");
        if (musicDefinitionFile) { ++report.MusicDefinitionFilesSeen; }
        if (!definitionFile && !musicDefinitionFile && !scriptedEffectFile) { continue; }
        TResult<FResourceBlob> loaded = resources.Load(logicalName);
        if (!loaded.IsOk()) { continue; }
        const FByteArray& bytes = loaded.Value().Bytes;
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const std::vector<FDefinitionBlock> audioBlocks = ExtractBlocks(text, {"soundeffect", "soundtrack"});
        const size_t embeddedCount = ImportEmbeddedEffectSounds(text, audio, report);
        if (audioBlocks.empty() && embeddedCount == 0) { continue; }
        ++report.FilesScanned;
        for (const FDefinitionBlock& block : audioBlocks)
        {
            if (block.Type == "soundeffect")
            {
                int32 effectId = -1;
                FAudioCueDefinition cue;
                if (!ParseSoundEffect(block, effectId, cue, true)) { ++report.DefinitionsSkipped; continue; }
                const std::string cueName = FLegacyAudioResolver::CueNameForEffect(effectId);
                audio.RegisterCue(cueName, std::move(cue));
                audio.RegisterLegacyEffect(effectId, cueName);
                ++report.SoundEffectsImported;
            }
            else
            {
                std::string name;
                FMusicTrackDefinition track;
                if (!ParseSoundtrack(block, record.RelativePath.stem().string(), name, track)) { ++report.DefinitionsSkipped; continue; }
                track.Asset = ResolveAudioAsset(resources, record, std::move(track.Asset));
                audio.RegisterMusicTrack(std::move(name), std::move(track));
                ++report.MusicTracksImported;
            }
        }
    }
    for (const FFileRecord& record : resources.Catalog().All())
    {
        const std::string logicalName = record.RelativePath.generic_string();
        if (!IsMusicPath(logicalName) || !IsAudioAsset(logicalName)) { continue; }
        ++report.RawMusicAssetsSeen;
        FMusicTrackDefinition track;
        track.Asset = logicalName;
        bool imported = false;
        for (const std::string& alias : MusicAliases(logicalName))
        {
            if (audio.RegisterMusicTrackIfMissing(alias, track)) { ++report.MusicAliasesImported; imported = true; }
        }
        if (imported) { ++report.RawMusicAssetsImported; }
    }
    if (logger)
    {
        logger->Info("audio definitions imported: files=" + std::to_string(report.FilesScanned) + ", direct=" + std::to_string(report.SoundEffectsImported) + ", scripted=" + std::to_string(report.ScriptedEffectsImported) + ", music_defs=" + std::to_string(report.MusicTracksImported) + ", music_def_files=" + std::to_string(report.MusicDefinitionFilesSeen) + ", raw_music=" + std::to_string(report.RawMusicAssetsImported) + "/" + std::to_string(report.RawMusicAssetsSeen) + ", music_aliases=" + std::to_string(report.MusicAliasesImported) + ", skipped=" + std::to_string(report.DefinitionsSkipped));
        if (report.MusicTracksImported == 0 && report.RawMusicAssetsImported == 0) { logger->Warning("no playable music was discovered under Sounds\\Music"); }
        else if (report.MusicDefinitionFilesSeen > 0 && report.MusicTracksImported == 0) { logger->Warning("music definition files were found, but no SoundTrack block was parsed; raw music aliases will be used"); }
    }
    return report;
}
