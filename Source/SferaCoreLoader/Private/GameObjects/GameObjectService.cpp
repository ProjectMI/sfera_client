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
