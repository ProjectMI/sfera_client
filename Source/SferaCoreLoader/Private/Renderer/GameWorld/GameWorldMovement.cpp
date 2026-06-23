#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

void FD3D9GameWorldScene::Impl::SnapToGround()
{
    float height = 0.0f;
    if (TerrainHeightAt(SpawnX, SpawnZ, SpawnY, height))
    {
        SpawnY = height;
    }
}

bool FD3D9GameWorldScene::Impl::SupportHeightAt(float x, float z, float FeetY, float& OutY, FVector3* OutNormal) const
{
    // Window centred on the FEET (step up or down). A surface only supports
    // the body if it lies within this window — otherwise there is nothing to
    // stand on here (the body should fall, not snap down to far-below terrain,
    // which looked like falling through the model when standing on an object).
    const float ReachUp = FeetY - Config.MaxStepHeight;
    const float ReachDown = FeetY + Config.MaxStepHeight;
    bool found = false;
    float floor = 0.0f;
    FVector3 normal{0.0f, -1.0f, 0.0f};
    float terrain = 0.0f;
    FVector3 TerrainNormal{};
    if (TerrainSurfaceAt(x, z, terrain, TerrainNormal) && terrain >= ReachUp && terrain <= ReachDown)
    {
        floor = terrain;
        normal = TerrainNormal;
        found = true;
    }
    float obj = 0.0f;
    FVector3 ObjNormal{};
    if (StaticFloorHeightAt(x, z, ReachUp, ReachDown, obj, &ObjNormal) && (!found || obj < floor))
    {
        floor = obj;  // stand on the object surface (higher than terrain)
        normal = ObjNormal;
        found = true;
    }
    if (found)
    {
        OutY = floor;
        if (OutNormal)
        {
            *OutNormal = normal;
        }
    }
    return found;
}

bool FD3D9GameWorldScene::Impl::TryMoveTo(float x, float z)
{
    if (Grounded)
    {
        // On the ground the body rests on the support surface (step up/down
        // onto terrain or objects within max_step_height).
        float GroundY = 0.0f;
        if (!SupportHeightAt(x, z, SpawnY, GroundY))
        {
            return false;
        }
        if (std::abs(GroundY - SpawnY) > Config.MaxStepHeight ||
            CollidesWithStatic(x, GroundY, z))
        {
            return false;
        }
        SpawnX = x;
        SpawnY = GroundY;
        SpawnZ = z;
    }
    else
    {
        // Airborne: keep the Jump-controlled height, just move horizontally.
        // Do NOT require a support surface here — once you Jump higher than a
        // step the ground falls outside the support window, and requiring it
        // would freeze all horizontal motion mid-air ("Jump in place").
        if (CollidesWithStatic(x, SpawnY, z))
        {
            return false;
        }
        SpawnX = x;
        SpawnZ = z;
    }
    return true;
}

void FD3D9GameWorldScene::Impl::Jump()
{
    if (Grounded)
    {
        VelocityY = Config.JumpImpulse;
        Grounded = false;
    }
}

void FD3D9GameWorldScene::Impl::ApplySlopeSlide(float DeltaSeconds)
{
    if (!Grounded || DeltaSeconds <= 0.0f)
    {
        return;
    }
    float FloorY = 0.0f;
    FVector3 n{};
    if (!SupportHeightAt(SpawnX, SpawnZ, SpawnY, FloorY, &n))
    {
        return;
    }
    const float ny = std::abs(n.Y);
    if (ny >= Config.SlopeSlideNormalY || ny <= 0.0001f)
    {
        return;  // gentle enough to stand on (or degenerate)
    }
    const float g = Config.JumpGravity;
    const float tx = -g * n.Y * n.X;
    const float tz = -g * n.Y * n.Z;
    const float hlen = std::sqrt(tx * tx + tz * tz);
    if (hlen < 1e-4f)
    {
        return;
    }
    const float speed = g * std::sqrt((std::max)(0.0f, 1.0f - ny * ny)) * Config.SlopeSlideFactor;
    const float disp = speed * DeltaSeconds;
    TryMoveTo(SpawnX + (tx / hlen) * disp, SpawnZ + (tz / hlen) * disp);
}

void FD3D9GameWorldScene::Impl::UpdateVertical(float DeltaSeconds)
{
    if (Grounded || DeltaSeconds <= 0.0f)
    {
        return;
    }
    VelocityY += Config.JumpGravity * DeltaSeconds;
    const float PrevY = SpawnY;
    SpawnY += VelocityY * DeltaSeconds;
    if (VelocityY < 0.0f)
    {
        return;  // still rising (Jump apex not reached); can't land
    }
    // Land on the highest surface (terrain or a static object) crossed during
    // this descent, so you can land on top of objects, not only the terrain.
    float floor = 0.0f;
    bool found = TerrainHeightAt(SpawnX, SpawnZ, SpawnY, floor);
    float obj = 0.0f;
    if (StaticFloorHeightAt(SpawnX, SpawnZ, PrevY - 0.05f, SpawnY + 0.05f, obj) &&
    (!found || obj < floor))
    {
        floor = obj;
        found = true;
    }
    if (found && SpawnY >= floor)
    {
        SpawnY = floor;
        VelocityY = 0.0f;
        Grounded = true;
    }
}

bool FD3D9GameWorldScene::Impl::PumpWorldEntryLoad(std::wstring& error)
{
    if (!WorldEntryLoadPending)
    {
        return false;
    }
    try
    {
        if (WorldEntryLoadStage == 0)
        {
            LoadWorldShaders();
            WorldEntryLoadStage = 1;
            return true;
        }
        if (WorldEntryLoadStage == 1)
        {
            TerrainMicrotexture = LoadMtxTexture(Device, ResolveConfiguredPath(Config.TerrainMicrotexture));
            WorldEntryLoadStage = 2;
            return true;
        }
        if (WorldEntryLoadStage == 2)
        {
            SkyTexture = LoadCachedDdsTexture(ResolveConfiguredPath(Config.SkyTexture));
            WorldEntryLoadStage = 3;
            return true;
        }
        if (WorldEntryLoadStage == 3)
        {
            const auto WaterPath = ResolveOptionalPath("landscape/river1a_00.dds");
            if (!WaterPath.empty())
            {
                WaterTexture = LoadCachedDdsTexture(WaterPath);
            }
            WorldEntryLoadStage = 4;
            return true;
        }
        if (WorldEntryLoadStage == 4)
        {
            TerrainStreamingPending = !LoadVisibleTerrain((std::max)(1, Config.TerrainInitialLoadBudget));
            WorldEntryLoadStage = 5;
            return true;
        }
        if (WorldEntryLoadStage == 5)
        {
            SnapToGround();
            WorldEntryLoadStage = 6;
            return true;
        }
        if (WorldEntryLoadStage == 6)
        {
            if (PendingPlayerModel.IsValid())
            {
                LoadPlayerModel(PendingPlayerModel);
                PendingPlayerModel = FSkinnedCharacterModel{};
            }
            WorldEntryLoadStage = 7;
            return true;
        }
        if (WorldEntryLoadStage == 7)
        {
            if (!PumpTerrainGpuUploads(1))
            {
                return true;
            }
            WorldEntryLoadStage = 8;
            return true;
        }
        DeferredGrassLoadPending = Config.GrassQuality > 0;
        DeferredStaticPlacementsPending = true;
        DeferredStaticInstancesPending = false;
        DeferredReflectionTargetPending = true;
        WorldEntryLoadPending = false;
        return false;
    }
    catch (const std::exception& ex)
    {
        WorldEntryLoadPending = false;
        AssignError(error, std::string("game world entry load failed: ") + ex.what());
        return false;
    }
}

bool FD3D9GameWorldScene::Impl::Update(float DeltaSeconds, const FGameMovementInput& input, std::wstring& error)
{
    if (!Initialized)
    {
        error = L"game world scene is not initialized";
        return false;
    }
    ElapsedSeconds += (std::max)(0.0f, DeltaSeconds);
    SetGameTime(GameTimeFraction + (std::max)(0.0f, DeltaSeconds) * 12.0f / 86400.0f);
    if (PumpWorldEntryLoad(error))
    {
        return error.empty();
    }
    if (!error.empty())
    {
        return false;
    }
    if (!PumpTerrainGpuUploads(1))
    {
        return true;
    }
    if (TerrainStreamingPending)
    {
        try
        {
            TerrainStreamingPending = !LoadVisibleTerrain((std::max)(1, Config.TerrainStreamLoadBudget));
            PumpTerrainGpuUploads(1);
        }
        catch (const std::exception& ex)
        {
            TerrainStreamingPending = false;
            if (Logger)
            {
                Logger->Warning(std::string("deferred terrain stream failed: ") + ex.what());
            }
        }
    }
    else if (DeferredStaticPlacementsPending)
    {
        try
        {
            if (!StaticPlacementWorkerStarted)
            {
                BeginStaticPlacementLoadAsync();
            }
            if (PollStaticPlacementLoad())
            {
                DeferredStaticPlacementsPending = false;
                DeferredStaticInstancesPending = true;
            }
        }
        catch (const std::exception& ex)
        {
            DeferredStaticPlacementsPending = false;
            JoinStaticPlacementWorker();
            if (Logger)
            {
                Logger->Warning(std::string("deferred static placement load failed: ") + ex.what());
            }
        }
    }
    else if (DeferredStaticInstancesPending)
    {
        try
        {
            DeferredStaticInstancesPending = !LoadVisibleStaticObjects((std::max)(1, Config.StaticStreamModelBudget));
        }
        catch (const std::exception& ex)
        {
            DeferredStaticInstancesPending = false;
            if (Logger)
            {
                Logger->Warning(std::string("deferred static object load failed: ") + ex.what());
            }
        }
    }
    else if (DeferredGrassLoadPending)
    {
        try
        {
            DeferredGrassLoadPending = !LoadVisibleGrass((std::numeric_limits<int>::max)());
        }
        catch (const std::exception& ex)
        {
            DeferredGrassLoadPending = false;
            if (Logger)
            {
                Logger->Warning(std::string("deferred grass load failed: ") + ex.what());
            }
        }
    }
    else if (DeferredReflectionTargetPending)
    {
        CreateReflectionTarget();
        DeferredReflectionTargetPending = false;
    }
    const float Forward = (input.Forward ? 1.0f : 0.0f) - (input.Backward ? 1.0f : 0.0f);
    const float right = (input.StrafeRight ? 1.0f : 0.0f) - (input.StrafeLeft ? 1.0f : 0.0f);
    const float InputLength = std::sqrt(Forward * Forward + right * right);
    const bool moving = InputLength > 0.0001f && DeltaSeconds > 0.0f;
    UpdatePlayerAnimation(DeltaSeconds, moving, input.Run);
    UpdateVertical(DeltaSeconds);

    // ControlMove keeps the own character and camera facing the same way.
    SpawnAngle = -CameraYaw;

    // the horizontal speed g_85B8 is reset to 0 and re-derived from the held
    // movement keys, then the engine integrates it (the Jump only sets the
    // vertical field 0x28C, never the horizontal). So there is no momentum/glide
    // — releasing the keys stops you, even mid-air — but holding a direction
    // through a Jump keeps carrying you forward (that is the "inertia").
    if (moving)
    {
        const float NormalizedForward = Forward / InputLength;
        const float NormalizedRight = right / InputLength;
        const float speed = Config.WalkSpeed * (input.Run ? Config.RunMultiplier : 1.0f);
        VelocityX = (std::sin(CameraYaw) * NormalizedForward +
        std::cos(CameraYaw) * NormalizedRight) * speed;
        VelocityZ = (std::cos(CameraYaw) * NormalizedForward -
        std::sin(CameraYaw) * NormalizedRight) * speed;
    }
    else
    {
        VelocityX = 0.0f;
        VelocityZ = 0.0f;
        if (Grounded)
        {
            // Standing still on a steep floor: gravity drags you downhill (the
            // only time the auto-slide applies, so it never fights a climb).
            ApplySlopeSlide(DeltaSeconds);
        }
        return true;
    }

    if (DeltaSeconds <= 0.0f)
    {
        return true;
    }

    const float DispX = VelocityX * DeltaSeconds;
    const float DispZ = VelocityZ * DeltaSeconds;
    const float distance = std::sqrt(DispX * DispX + DispZ * DispZ);
    const int MovementSteps = (std::max)(1, static_cast<int>(std::ceil(distance / Config.MovementCollisionStep)));
    const float StepX = DispX / static_cast<float>(MovementSteps);
    const float StepZ = DispZ / static_cast<float>(MovementSteps);
    for (int step = 0; step < MovementSteps; ++step)
    {
        const float PreviousX = SpawnX;
        const float PreviousZ = SpawnZ;
        if (TryMoveTo(PreviousX + StepX, PreviousZ + StepZ))
        {
            continue;
        }
        const bool MovedX = TryMoveTo(PreviousX + StepX, PreviousZ);
        const float SlideX = SpawnX;
        const float SlideZ = SpawnZ;
        if (!TryMoveTo(SlideX, SlideZ + StepZ) && !MovedX)
        {
            break;
        }
    }

    const int CenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    if (CenterRow != TerrainCenterRow || CenterColumn != TerrainCenterColumn)
    {
        try
        {
            TerrainStreamingPending = !LoadVisibleTerrain((std::max)(1, Config.TerrainStreamLoadBudget));
            PumpTerrainGpuUploads(1);
            DeferredStaticInstancesPending = true;
            DeferredGrassLoadPending = Config.GrassQuality > 0;
        } catch (const std::exception& ex)
        {
            AssignError(error, std::string("game world terrain update failed: ") + ex.what());
            return false;
        }
    }
    const float GrassDx = SpawnX - GrassAnchorX;
    const float GrassDz = SpawnZ - GrassAnchorZ;
    if (Config.GrassQuality > 0 &&
    (!GrassAnchorValid ||
    GrassDx * GrassDx + GrassDz * GrassDz >=
    Config.GrassGenerationMargin * Config.GrassGenerationMargin))
    {
        try
        {
            DeferredGrassLoadPending = !LoadVisibleGrass((std::numeric_limits<int>::max)());
        } catch (const std::exception& ex)
        {
            DeferredGrassLoadPending = false;
            if (Logger)
            {
                Logger->Warning(std::string("game world grass update skipped: ") + ex.what());
            }
        }
    }
    return true;
}

void FD3D9GameWorldScene::Impl::RotateView(float MouseDx, float MouseDy)
{
    CameraYaw += MouseDx * Config.CameraTurnSpeed;
    while (CameraYaw > kPi)
    {
        CameraYaw -= 2.0f * kPi;
    }
    while (CameraYaw < -kPi)
    {
        CameraYaw += 2.0f * kPi;
    }
    CameraPitch = std::clamp(
    CameraPitch - MouseDy * Config.CameraPitchSpeed,
    Config.CameraMinPitch,
    Config.CameraMaxPitch);
    SpawnAngle = -CameraYaw;
}

FGameWorldPosition FD3D9GameWorldScene::Impl::Position() const
{
    return FGameWorldPosition{SpawnX, SpawnY, SpawnZ, SpawnAngle};
}
