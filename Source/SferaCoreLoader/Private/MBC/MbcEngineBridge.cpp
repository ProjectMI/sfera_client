#include "MBC/MbcEngineBridge.h"
#include <algorithm>
#include <cctype>
#include <utility>

static std::string TrimBridge(std::string s)
{
    auto ns = [](unsigned char c)
    {
        return !std::isspace(c) && c != '\0';
    };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

std::string FMbcEngineBridge::ReadSliceString(const FMbcNativeContext& ctx, const FMbcSlice& slice)
{
    if (!ctx.Data || slice.Offset >= ctx.Data->size()) { return {}; }

    size_t n = std::min<size_t>(slice.Length, ctx.Data->size() - slice.Offset);
    std::string text;
    text.reserve(n);

    for (size_t i = 0; i < n && (*ctx.Data)[slice.Offset + i] != 0; ++i)
    {
        text.push_back(static_cast<char>((*ctx.Data)[slice.Offset + i]));
    }

    return TrimBridge(std::move(text));
}

std::string FMbcEngineBridge::BestStringArg(const FMbcNativeContext& ctx, std::string fallback)
{
    for (auto it = ctx.Args.rbegin(); it != ctx.Args.rend(); ++it)
    {
        if (it->Type == Mbc::TypeSlice && it->Slice.Length > 0)
        {
            std::string text = ReadSliceString(ctx, it->Slice);

            if (!text.empty()) { return text; }

            return "slice@" + std::to_string(it->Slice.Offset) + ":" + std::to_string(it->Slice.Length);
        }
    }

    return fallback;
}

int32 FMbcEngineBridge::BestIntArg(const FMbcNativeContext& ctx, size_t reverseIndex, int32 fallback)
{
    size_t seen = 0;

    for (auto it = ctx.Args.rbegin(); it != ctx.Args.rend(); ++it)
    {
        if (it->Type == Mbc::TypeInt || it->Type == Mbc::TypeChar)
        {
            if (seen == reverseIndex)
            {
                return it->IntValue;
            }

            ++seen;
        }
    }

    return fallback;
}

float FMbcEngineBridge::BestFloatArg(const FMbcNativeContext& ctx, size_t reverseIndex, float fallback)
{
    size_t seen = 0;

    for (auto it = ctx.Args.rbegin(); it != ctx.Args.rend(); ++it)
    {
        if (it->Type == Mbc::TypeFloat || it->Type == Mbc::TypeInt || it->Type == Mbc::TypeChar)
        {
            if (seen == reverseIndex) { return it->Type == Mbc::TypeFloat ? it->FloatValue : static_cast<float>(it->IntValue); }

            ++seen;
        }
    }

    return fallback;
}

void FMbcEngineBridge::Register(FMbcNativeRegistry& registry, FGameObjectService* objects, FWorldScene* world, const FResourceManager* resources, FLogger* logger)
{
    registry.Register("object_create", [objects, world, logger](FMbcNativeContext& ctx)
    {
        std::string archetype = BestStringArg(ctx, "builtin.object_create");
        uint32 handle = objects ? objects->CreateObject(archetype, EGameObjectKind::ScriptProxy) : 0;

        if (world && handle)
        {
            world->SyncObject(handle, {}, 32.0f, archetype);
        }

        ctx.ReturnValue = FMbcValue::Int(static_cast<int32>(handle));

        if (logger && handle)
        {
            logger->Info("MBC builtin object_create: " + archetype + " -> handle " + std::to_string(handle));
        }

        return FStatus::Ok();
    });
    registry.Register("sprite_create_or_update", [objects, world](FMbcNativeContext& ctx)
    {
        std::string archetype = BestStringArg(ctx, "builtin.sprite");
        uint32 handle = objects ? objects->CreateObject(archetype, EGameObjectKind::StaticObject) : 0;

        if (world && handle)
        {
            world->SyncObject(handle, {}, 32.0f, archetype);
        }

        ctx.ReturnValue = FMbcValue::Int(static_cast<int32>(handle));
        return FStatus::Ok();
    });
    registry.Register("object_release_type4", [objects, world](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));

        if (world)
        {
            world->RemoveObject(h);
        }

        if (objects)
        {
            objects->DestroyObject(h);
        }

        return FStatus::Ok();
    });
    registry.Register("object_delete_type0", [objects, world](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));

        if (world)
        {
            world->RemoveObject(h);
        }

        if (objects)
        {
            objects->DestroyObject(h);
        }

        return FStatus::Ok();
    });
    registry.Register("object_remove_type5", [objects, world](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));

        if (world)
        {
            world->RemoveObject(h);
        }

        if (objects)
        {
            objects->DestroyObject(h);
        }

        return FStatus::Ok();
    });
    registry.Register("object_set_pos_xyz", [objects, world](FMbcNativeContext& ctx)
    {
        if (!objects)
        {
            return FStatus::Ok();
        }

        uint32 h = static_cast<uint32>(BestIntArg(ctx, 3, 0));
        FVector3 p
        {
            BestFloatArg(ctx, 2, 0.0f), BestFloatArg(ctx, 1, 0.0f), BestFloatArg(ctx, 0, 0.0f)
        };
        objects->SetPosition(h, p);

        if (world)
        {
            world->SyncObject(h, p, 32.0f);
        }

        return FStatus::Ok();
    });
    registry.Register("object_add_pos_xyz", [objects, world](FMbcNativeContext& ctx)
    {
        if (!objects)
        {
            return FStatus::Ok();
        }

        uint32 h = static_cast<uint32>(BestIntArg(ctx, 3, 0));
        FVector3 d
        {
            BestFloatArg(ctx, 2, 0.0f), BestFloatArg(ctx, 1, 0.0f), BestFloatArg(ctx, 0, 0.0f)
        };
        objects->AddPosition(h, d);

        if (world)
        {
            if (const auto* o = objects->Registry().Find(h))
            {
                world->SyncObject(h, o->Position, 32.0f, o->Archetype);
            }
        }

        return FStatus::Ok();
    });
    registry.Register("object_get_x", [objects](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));
        const auto* o = objects ? objects->Registry().Find(h) : nullptr;
        ctx.ReturnValue = FMbcValue::Float(o ? o->Position.X : 0.0f);
        return FStatus::Ok();
    });
    registry.Register("object_get_y", [objects](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));
        const auto* o = objects ? objects->Registry().Find(h) : nullptr;
        ctx.ReturnValue = FMbcValue::Float(o ? o->Position.Y : 0.0f);
        return FStatus::Ok();
    });
    registry.Register("object_get_z", [objects](FMbcNativeContext& ctx)
    {
        uint32 h = static_cast<uint32>(BestIntArg(ctx, 0, 0));
        const auto* o = objects ? objects->Registry().Find(h) : nullptr;
        ctx.ReturnValue = FMbcValue::Float(o ? o->Position.Z : 0.0f);
        return FStatus::Ok();
    });
    registry.Register("object_get_position_vec3", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(0);
        return FStatus::Ok();
    });
    registry.Register("resource_load_by_name", [resources](FMbcNativeContext& ctx)
    {
        std::string name = BestStringArg(ctx, {});
        bool found = resources && !name.empty() && resources->Catalog().FindByLogicalName(name).has_value();
        ctx.ReturnValue = FMbcValue::Int(found ? 1 : 0);
        return FStatus::Ok();
    });
    registry.Register("path_exists", [resources](FMbcNativeContext& ctx)
    {
        std::string name = BestStringArg(ctx, {});
        bool found = resources && !name.empty() && resources->Catalog().FindByLogicalName(name).has_value();
        ctx.ReturnValue = FMbcValue::Int(found ? 1 : 0);
        return FStatus::Ok();
    });
    registry.Register("path_is_directory", [resources](FMbcNativeContext& ctx)
    {
        std::string name = BestStringArg(ctx, {});
        bool found = false;

        if (resources && !name.empty())
        {
            std::string prefix = name;
            std::replace(prefix.begin(), prefix.end(), '\\', '/');

            if (!prefix.empty() && prefix.back() != '/')
            {
                prefix += '/';
            }

            for (const auto& record : resources->Catalog().All())
            {
                std::string p = record.RelativePath.generic_string();
                std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                std::string q = prefix;
                std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

                if (p.find(q) == 0)
                {
                    found = true;
                    break;
                }
            }
        }

        ctx.ReturnValue = FMbcValue::Int(found ? 1 : 0);
        return FStatus::Ok();
    });
    registry.Register("query_runtime_ready", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(1);
        return FStatus::Ok();
    });
    registry.Register("map_load", [world](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(world ? static_cast<int32>(world->Stats().MapPresentCells) : 0);
        return FStatus::Ok();
    });
    registry.Register("map_save", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(0);
        return FStatus::Ok();
    });
    registry.Register("map_pic_size", [world](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(world ? static_cast<int32>(world->Stats().MapCellCount) : 0);
        return FStatus::Ok();
    });
    registry.Register("world_find_zone", [world](FMbcNativeContext& ctx)
    {
        FVector3 p
        {
            BestFloatArg(ctx, 2, 0.0f), BestFloatArg(ctx, 1, 0.0f), BestFloatArg(ctx, 0, 0.0f)
        };
        const auto* zone = world ? world->FindZone(p) : nullptr;
        ctx.ReturnValue = FMbcValue::Int(zone ? static_cast<int32>(zone->Index) : -1);
        return FStatus::Ok();
    });
    registry.Register("world_query_contours", [world](FMbcNativeContext& ctx)
    {
        float z = BestFloatArg(ctx, 0, 0.0f);
        float x = BestFloatArg(ctx, 1, 0.0f);
        FBox2 a;
        a.Min =
        {
            x - 128.0f, z - 128.0f
        };
        a.Max =
        {
            x + 128.0f, z + 128.0f
        };
        ctx.ReturnValue = FMbcValue::Int(world ? static_cast<int32>(world->QueryContours(a).size()) : 0);
        return FStatus::Ok();
    });
    registry.Register("get_game_time", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(0);
        return FStatus::Ok();
    });
    registry.Register("current_process_id", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(0);
        return FStatus::Ok();
    });
    registry.Register("last_process_result", [](FMbcNativeContext& ctx)
    {
        ctx.ReturnValue = FMbcValue::Int(0);
        return FStatus::Ok();
    });
}
