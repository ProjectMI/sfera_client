#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include <cmath>
#include <limits>

void FD3D9GameWorldScene::Impl::SnapToGround()
{
    float height = 0.0f;
    if (TerrainHeightAt(SpawnX, SpawnZ, SpawnY, height))
    {
        SpawnY = height;
    }
}

void FD3D9GameWorldScene::Impl::SetPlayerWorldPosition(const FGameWorldPosition& Position)
{
    if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) || !std::isfinite(Position.Angle))
    {
        return;
    }

    if (std::abs(Position.X) > 20000.0 || std::abs(Position.Y) > 20000.0 || std::abs(Position.Z) > 20000.0)
    {
        return;
    }

    SpawnX = static_cast<float>(Position.X);
    SpawnY = static_cast<float>(Position.Y);
    SpawnZ = static_cast<float>(Position.Z);
    SpawnAngle = static_cast<float>(Position.Angle);
    CameraYaw = -SpawnAngle;
    VelocityX = 0.0f;
    VelocityY = 0.0f;
    VelocityZ = 0.0f;
    Grounded = true;

    TerrainCenterRow = -1;
    TerrainCenterColumn = -1;
    StreamingGuardRow = (std::numeric_limits<int>::min)();
    StreamingGuardColumn = (std::numeric_limits<int>::min)();
    StaticVisibilityPlanReady = false;
    GrassAnchorValid = false;
    GrassCenterX = (std::numeric_limits<int>::min)();
    GrassCenterZ = (std::numeric_limits<int>::min)();
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
    return TryMoveTo(x, z, SpawnY, SpawnY);
}

bool FD3D9GameWorldScene::Impl::TryMoveTo(float x, float z, float fromY, float toY)
{
    const bool contoursReady = HasContourCollision();
    if (CollidesWithContours(SpawnX, SpawnZ, x, z, Config.PlayerCollisionRadius))
    {
        return false;
    }

    if (Grounded)
    {
        float GroundY = 0.0f;
        if (!SupportHeightAt(x, z, SpawnY, GroundY))
        {
            if (CollidesWithModelContacts(SpawnX, SpawnZ, x, SpawnY, z))
            {
                VelocityY = (std::max)(VelocityY, 0.0f);
                Grounded = false;
                return false;
            }
            if (!contoursReady && ModelCollisionProxies.empty() && CollidesWithStatic(x, SpawnY, z))
            {
                VelocityY = (std::max)(VelocityY, 0.0f);
                Grounded = false;
                return false;
            }
            SpawnX = x;
            SpawnZ = z;
            VelocityY = (std::max)(VelocityY, 0.0f);
            Grounded = false;
            return true;
        }

        const float deltaY = GroundY - SpawnY;
        if (deltaY < -Config.MaxStepHeight)
        {
            return false;
        }
        if (deltaY > Config.MaxStepHeight)
        {
            if (CollidesWithModelContacts(SpawnX, SpawnZ, x, SpawnY, z))
            {
                VelocityY = (std::max)(VelocityY, 0.0f);
                Grounded = false;
                return false;
            }
            if (!contoursReady && ModelCollisionProxies.empty() && CollidesWithStatic(x, SpawnY, z))
            {
                VelocityY = (std::max)(VelocityY, 0.0f);
                Grounded = false;
                return false;
            }
            SpawnX = x;
            SpawnZ = z;
            VelocityY = (std::max)(VelocityY, 0.0f);
            Grounded = false;
            return true;
        }
        if (CollidesWithModelContacts(SpawnX, SpawnZ, x, GroundY, z))
        {
            return false;
        }
        if (!contoursReady && ModelCollisionProxies.empty() && CollidesWithStatic(x, GroundY, z))
        {
            return false;
        }
        SpawnX = x;
        SpawnY = GroundY;
        SpawnZ = z;
        return true;
    }

    if (CollidesWithModelContactsSwept(SpawnX, fromY, SpawnZ, x, toY, z, true))
    {
        return false;
    }
    if (!contoursReady && ModelCollisionProxies.empty() && CollidesWithStatic(x, SpawnY, z))
    {
        return false;
    }
    SpawnX = x;
    SpawnZ = z;
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
        VelocityY = (std::max)(VelocityY, 0.0f);
        Grounded = false;
        return;
    }
    if (FloorY - SpawnY > Config.MaxStepHeight)
    {
        VelocityY = (std::max)(VelocityY, 0.0f);
        Grounded = false;
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
        UpdateNpcAnimation(DeltaSeconds);
    }
    RequestCollisionSnapshotAround(SpawnX, SpawnZ);

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
            ApplySlopeSlide(DeltaSeconds);
        }
        if (!Grounded)
        {
            UpdateVertical(DeltaSeconds);
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
    const bool AirborneAtMoveStart = !Grounded;
    const float MoveStartY = SpawnY;
    const float MoveEndY = AirborneAtMoveStart ? SpawnY + (VelocityY + Config.JumpGravity * DeltaSeconds) * DeltaSeconds : SpawnY;
    const int MovementSteps = (std::max)(1, static_cast<int>(std::ceil(distance / Config.MovementCollisionStep)));
    const float StepX = DispX / static_cast<float>(MovementSteps);
    const float StepZ = DispZ / static_cast<float>(MovementSteps);
    {
        for (int step = 0; step < MovementSteps; ++step)
        {
            const float PreviousX = SpawnX;
            const float PreviousZ = SpawnZ;
            const float PreviousY = AirborneAtMoveStart ? MoveStartY + (MoveEndY - MoveStartY) * (static_cast<float>(step) / static_cast<float>(MovementSteps)) : SpawnY;
            const float TargetY = AirborneAtMoveStart ? MoveStartY + (MoveEndY - MoveStartY) * (static_cast<float>(step + 1) / static_cast<float>(MovementSteps)) : SpawnY;
            if (TryMoveTo(PreviousX + StepX, PreviousZ + StepZ, PreviousY, TargetY))
            {
                continue;
            }
            const bool MovedX = TryMoveTo(PreviousX + StepX, PreviousZ, PreviousY, TargetY);
            const float SlideX = SpawnX;
            const float SlideZ = SpawnZ;
            if (!TryMoveTo(SlideX, SlideZ + StepZ, PreviousY, TargetY) && !MovedX)
            {
                break;
            }
        }
    }

    UpdateVertical(DeltaSeconds);

    const int CenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    bool GrassLoadedThisFrame = false;
    bool StreamingUpdatedThisFrame = false;
    if (CenterRow != TerrainCenterRow || CenterColumn != TerrainCenterColumn)
    {
        try
        {
            LoadVisibleTerrain();
            LoadVisibleStaticObjects();
            if (Config.GrassQuality > 0)
            {
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

