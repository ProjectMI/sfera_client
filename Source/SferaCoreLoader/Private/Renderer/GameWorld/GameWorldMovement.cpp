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

bool FD3D9GameWorldScene::Impl::Update(float DeltaSeconds, const FGameMovementInput& input, std::wstring& error)
{
    FScopedDurationLog UpdateProbe(Logger, "gameworld.Update", 10.0, "pos=(" + FormatDurationLogValue(SpawnX) + "," + FormatDurationLogValue(SpawnZ) + ") delta=" + FormatDurationLogValue(static_cast<double>(DeltaSeconds) * 1000.0));
    if (!Initialized)
    {
        error = L"game world scene is not initialized";
        return false;
    }
    ElapsedSeconds += (std::max)(0.0f, DeltaSeconds);
    SetGameTime(GameTimeFraction + (std::max)(0.0f, DeltaSeconds) * 12.0f / 86400.0f);
    const float Forward = (input.Forward ? 1.0f : 0.0f) - (input.Backward ? 1.0f : 0.0f);
    const float right = (input.StrafeRight ? 1.0f : 0.0f) - (input.StrafeLeft ? 1.0f : 0.0f);
    const float InputLength = std::sqrt(Forward * Forward + right * right);
    const bool moving = InputLength > 0.0001f && DeltaSeconds > 0.0f;
    UpdatePlayerAnimation(DeltaSeconds, moving, input.Run);
    {
        FScopedDurationLog Probe(Logger, "gameworld.UpdateNpcAnimation", 8.0);
        UpdateNpcAnimation(DeltaSeconds);
    }
    {
        FScopedDurationLog Probe(Logger, "gameworld.UpdateVertical", 1.5);
        UpdateVertical(DeltaSeconds);
    }

    SpawnAngle = -CameraYaw;

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
            FScopedDurationLog Probe(Logger, "gameworld.ApplySlopeSlide", 1.5);
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
    {
        FScopedDurationLog Probe(Logger, "gameworld.MovementCollisionSteps", 2.0, "steps=" + std::to_string(MovementSteps));
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
    }

    const int CenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    bool GrassLoadedThisFrame = false;
    bool StreamingUpdatedThisFrame = false;
    if (CenterRow != TerrainCenterRow || CenterColumn != TerrainCenterColumn)
    {
        try
        {
            FScopedDurationLog Probe(Logger, "gameworld.StreamingTileCrossing", 8.0, "center=(" + std::to_string(CenterRow) + "," + std::to_string(CenterColumn) + ")");
            LoadVisibleTerrain();
            LoadVisibleStaticObjects();
            if (Config.GrassQuality > 0)
            {
                FScopedDurationLog GrassProbe(Logger, "gameworld.StreamingTileCrossing.LoadVisibleGrass", 5.0);
                LoadVisibleGrass();
                GrassLoadedThisFrame = true;
            }
            StreamingUpdatedThisFrame = true;
        } catch (const std::exception& ex)
        {
            AssignError(error, std::string("game world terrain update failed: ") + ex.what());
            return false;
        }
    }
    const float GrassDx = SpawnX - GrassAnchorX;
    const float GrassDz = SpawnZ - GrassAnchorZ;
    if (Config.GrassQuality > 0 &&
    !GrassLoadedThisFrame &&
    (GrassRefreshIncomplete ||
    !GrassAnchorValid ||
    GrassDx * GrassDx + GrassDz * GrassDz >=
    Config.GrassGenerationMargin * Config.GrassGenerationMargin))
    {
        try
        {
            FScopedDurationLog Probe(Logger, "gameworld.GrassAnchorRefresh", 5.0);
            LoadVisibleGrass();
            GrassLoadedThisFrame = true;
        } catch (const std::exception& ex)
        {
            if (Logger)
            {
                Logger->Warning(std::string("game world grass update skipped: ") + ex.what());
            }
        }
    }
    if (!StreamingUpdatedThisFrame && !GrassLoadedThisFrame)
    {
        FScopedDurationLog Probe(Logger, "gameworld.PreloadStreamingGuard.callsite", 2.0);
        PreloadStreamingGuard();
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
