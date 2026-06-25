#include "GameObjects/GameObjectService.h"

FGameObjectService::FGameObjectService(const FResourceManager& resources) : Resources(resources) {}
FStatus FGameObjectService::Initialize(FLogger* logger)
{
    ParamsLoaded = false;

    if (logger)
    {
        logger->Info("GameObjects initialized: params=deferred, live_objects=" + std::to_string(ObjectRegistry.Count()));
    }

    return FStatus::Ok();
}

FStatus FGameObjectService::EnsureParamsLoaded(FLogger* logger)
{
    if (ParamsLoaded)
    {
        return FStatus::Ok();
    }

    FStatus status = ParamManager.OpenKnownConfigs(Resources, logger);
    if (status.IsOk())
    {
        ParamsLoaded = true;
        if (logger)
        {
            logger->Info("GameObjects params loaded lazily: params=" + std::to_string(ParamManager.ObjectCount()));
        }
    }
    return status;
}

FObjectParamManager& FGameObjectService::Params()
{
    EnsureParamsLoaded(nullptr);
    return ParamManager;
}
uint32 FGameObjectService::CreateObject(std::string archetype, EGameObjectKind kind)
{
    FGameObjectState state;
    state.Archetype = std::move(archetype);
    state.Kind = kind;
    return ObjectRegistry.Create(std::move(state));
}
bool FGameObjectService::DestroyObject(uint32 handle) { return ObjectRegistry.Destroy(handle); }
void FGameObjectService::SetPosition(uint32 handle, FVector3 position)
{
    if (auto* object = ObjectRegistry.Find(handle))
    {
        object->Position = position;
    }
}
void FGameObjectService::AddPosition(uint32 handle, FVector3 delta)
{
    if (auto* object = ObjectRegistry.Find(handle))
    {
        object->Position.X += delta.X;
        object->Position.Y += delta.Y;
        object->Position.Z += delta.Z;
    }
}
void FGameObjectService::SetDirection(uint32 handle, FVector3 direction)
{
    if (auto* object = ObjectRegistry.Find(handle))
    {
        object->Direction = direction;
        object->Speed = std::sqrt(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z);
    }
}
void FGameObjectService::SetTrigger(uint32 handle, int32 trigger)
{
    if (auto* object = ObjectRegistry.Find(handle))
    {
        object->Trigger = trigger;
    }
}
float FGameObjectService::GetSpeed(uint32 handle) const
{
    if (const auto* object = ObjectRegistry.Find(handle))
    {
        return object->Speed;
    }

    return 0.0f;
}
int32 FGameObjectService::GetDirectionCode(uint32 handle) const
{
    const auto* object = ObjectRegistry.Find(handle);

    if (!object)
    {
        return 0;
    }

    if (std::abs(object->Direction.X) > std::abs(object->Direction.Z))
    {
        return object->Direction.X >= 0.0f ? 1 : 3;
    }

    return object->Direction.Z >= 0.0f ? 0 : 2;
}
void FGameObjectService::RegisterMbcNatives(FMbcNativeRegistry& registry, FLogger* logger)
{
    registry.Register("CreateObj", [this, logger](FMbcNativeContext& ctx)
    {
        uint32 handle = CreateObject("mbc.CreateObj", EGameObjectKind::ScriptProxy);
        ctx.ReturnValue = FMbcValue::Int(static_cast<int32>(handle));

        if (logger)
        {
            logger->Info("MBC native CreateObj -> handle " + std::to_string(handle));
        }

        return FStatus::Ok();
    });
    registry.Register("CreateObjWait", [this](FMbcNativeContext& ctx)
    {
        uint32 handle = CreateObject("mbc.CreateObjWait", EGameObjectKind::ScriptProxy);
        ctx.ReturnValue = FMbcValue::Int(static_cast<int32>(handle));
        return FStatus::Ok();
    });
    registry.Register("DestroyObj", [this](FMbcNativeContext& ctx)
    {
        if (!ctx.Args.empty())
        {
            DestroyObject(static_cast<uint32>(ctx.Args.front().IntValue));
        }

        return FStatus::Ok();
    });
    registry.Register("SetTrig", [this](FMbcNativeContext& ctx)
    {
        if (ctx.Args.size() >= 2)
        {
            SetTrigger(static_cast<uint32>(ctx.Args[0].IntValue), ctx.Args[1].IntValue);
        }

        return FStatus::Ok();
    });
    registry.RegisterRecoveredBoundary("CountAnim", EMbcNativeBoundaryReturn::IntZero);
    registry.Register("SpeedObj", [this](FMbcNativeContext& ctx)
    {
        uint32 handle = ctx.Args.empty() ? 0 : static_cast<uint32>(ctx.Args.front().IntValue);
        ctx.ReturnValue = FMbcValue::Float(GetSpeed(handle));
        return FStatus::Ok();
    });
    registry.Register("DirectOfObj", [this](FMbcNativeContext& ctx)
    {
        uint32 handle = ctx.Args.empty() ? 0 : static_cast<uint32>(ctx.Args.front().IntValue);
        ctx.ReturnValue = FMbcValue::Int(GetDirectionCode(handle));
        return FStatus::Ok();
    });
    registry.RegisterRecoveredBoundary("CenterObj");
    registry.RegisterRecoveredBoundary("InitAI");
    registry.RegisterRecoveredBoundary("InvertAI");
    registry.RegisterRecoveredBoundary("SetRespRadius");
}
