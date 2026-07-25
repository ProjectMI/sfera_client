#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include <cmath>
#include <limits>

void FD3D9GameWorldScene::Impl::SnapToGround()
{
    float GroundY = SpawnY;
    if (SupportHeightAt(SpawnX, SpawnZ, SpawnY, GroundY))
    {
        SpawnY = GroundY;
        VelocityY = 0.0f;
        Grounded = true;
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
    Grounded = false;
    PlayerCollisionNeedsRecovery = true;
    SnapToGround();
    TerrainCenterRow = -1;
    TerrainCenterColumn = -1;
    StreamingGuardRow = (std::numeric_limits<int>::min)();
    StreamingGuardColumn = (std::numeric_limits<int>::min)();
    StaticVisibilityPlanReady = false;
    GrassAnchorValid = false;
    GrassCenterX = (std::numeric_limits<int>::min)();
    GrassCenterZ = (std::numeric_limits<int>::min)();
}

bool FD3D9GameWorldScene::Impl::SupportHeightAt(float X, float Z, float FeetY, float& OutY, FVector3* OutNormal) const
{
    const float MinY = FeetY - Config.MaxStepHeight;
    const float MaxY = FeetY + Config.GroundSnapHeight;
    bool Found = false;
    float BestY = MaxY;
    FVector3 BestNormal{0.0f, 1.0f, 0.0f};
    auto ConsiderSupport = [&](float Height, const FVector3& Normal)
    {
        if (Found && Height >= BestY)
        {
            return;
        }
        Found = true;
        BestY = Height;
        BestNormal = Normal;
    };
    float TerrainY = 0.0f;
    FVector3 TerrainNormal{};
    if (TerrainSurfaceNearAt(X, Z, FeetY, TerrainY, TerrainNormal) && TerrainY >= MinY && TerrainY <= MaxY)
    {
        ConsiderSupport(TerrainY, TerrainNormal);
    }
    float StaticY = 0.0f;
    FVector3 StaticNormal{};
    if (StaticFloorHeightAt(X, Z, MinY, MaxY, StaticY, &StaticNormal))
    {
        ConsiderSupport(StaticY, StaticNormal);
    }
    if (!Found)
    {
        return false;
    }
    OutY = BestY;
    if (OutNormal)
    {
        *OutNormal = BestNormal;
    }
    return true;
}

bool FD3D9GameWorldScene::Impl::TryMoveTo(float X, float Z, FGameWorldCollisionHit* OutHit)
{
    const float StartX = SpawnX;
    const float StartY = SpawnY;
    const float StartZ = SpawnZ;
    const float DeltaX = X - StartX;
    const float DeltaZ = Z - StartZ;
    const float DistanceSquared = DeltaX * DeltaX + DeltaZ * DeltaZ;
    FGameWorldCollisionHit BlockingHit{};
    bool HasBlockingHit = false;
    auto RememberHit = [&](const FGameWorldCollisionHit& Hit)
    {
        if (!HasBlockingHit || Hit.Penetration > BlockingHit.Penetration)
        {
            BlockingHit = Hit;
            HasBlockingHit = true;
        }
    };
    auto TerrainPenetrates = [&](float FeetY, bool IgnoreRisingWalkableSupport, FGameWorldCollisionHit* Hit)
    {
        float TerrainY = 0.0f;
        FVector3 TerrainNormal{};
        if (!TerrainSurfaceNearAt(X, Z, FeetY, TerrainY, TerrainNormal) || FeetY <= TerrainY + Config.CollisionSkin)
        {
            return false;
        }
        const float Penetration = FeetY - TerrainY;
        const bool Walkable = std::abs(TerrainNormal.Y) >= Config.CollisionFloorNormalThreshold;
        const float RisingAllowance = Config.CollisionSkin + (std::max)(0.0f, -VelocityY) * Config.MaxSimulationStepSeconds;
        if (IgnoreRisingWalkableSupport && VelocityY < 0.0f && Walkable && Penetration <= RisingAllowance)
        {
            return false;
        }
        if (Hit)
        {
            Hit->Normal = FVector3{TerrainNormal.X, 0.0f, TerrainNormal.Z};
            Hit->Penetration = Penetration;
        }
        return true;
    };
    auto VerticalPathClear = [&](float PathX, float PathZ, float FromY, float ToY, FGameWorldCollisionHit* Hit)
    {
        const float Distance = std::abs(ToY - FromY);
        const float StepSize = (std::max)(0.05f, (std::min)(Config.MovementCollisionStep, Config.PlayerCollisionRadius * 0.5f));
        const int StepCount = (std::max)(1, static_cast<int>(std::ceil(Distance / StepSize)));
        for (int Step = 1; Step <= StepCount; ++Step)
        {
            const float CandidateY = FromY + (ToY - FromY) * static_cast<float>(Step) / static_cast<float>(StepCount);
            if (CapsuleOverlapsStatic(PathX, CandidateY, PathZ, Hit, true))
            {
                return false;
            }
        }
        return true;
    };
    auto CommitGrounded = [&](float FeetY)
    {
        SpawnX = X;
        SpawnY = FeetY;
        SpawnZ = Z;
        VelocityY = 0.0f;
        Grounded = true;
        return true;
    };
    if (!Grounded)
    {
        FGameWorldCollisionHit Hit{};
        if (TerrainPenetrates(StartY, true, &Hit) || CapsuleOverlapsStatic(X, StartY, Z, &Hit, true))
        {
            if (OutHit)
            {
                *OutHit = Hit;
            }
            return false;
        }
        SpawnX = X;
        SpawnZ = Z;
        return true;
    }
    float SupportY = StartY;
    FVector3 SupportNormal{0.0f, 1.0f, 0.0f};
    const bool HasSupport = SupportHeightAt(X, Z, StartY, SupportY, &SupportNormal);
    const bool HasWalkableSupport = HasSupport && std::abs(SupportNormal.Y) >= Config.CollisionFloorNormalThreshold;
    if (HasSupport)
    {
        const float Rise = StartY - SupportY;
        const float Drop = SupportY - StartY;
        if (HasWalkableSupport && Rise <= Config.MaxStepHeight + Config.CollisionSkin && Drop <= Config.GroundSnapHeight + Config.CollisionSkin)
        {
            FGameWorldCollisionHit Hit{};
            if (!TerrainPenetrates(SupportY, false, &Hit) && !CapsuleOverlapsStatic(X, SupportY, Z, &Hit, true))
            {
                return CommitGrounded(SupportY);
            }
            RememberHit(Hit);
        }
        if (!HasWalkableSupport)
        {
            FGameWorldCollisionHit SlopeHit{};
            SlopeHit.Normal = FVector3{SupportNormal.X, 0.0f, SupportNormal.Z};
            SlopeHit.Penetration = Config.CollisionSkin;
            RememberHit(SlopeHit);
        }
        if (SupportY > StartY + Config.CollisionSkin)
        {
            FGameWorldCollisionHit StepDownHit{};
            if (!TerrainPenetrates(StartY, false, &StepDownHit) && !CapsuleOverlapsStatic(X, StartY, Z, &StepDownHit, true))
            {
                SpawnX = X;
                SpawnZ = Z;
                VelocityY = 0.0f;
                return true;
            }
            RememberHit(StepDownHit);
        }
    }
    if (DistanceSquared > 0.0000001f)
    {
        const float Distance = std::sqrt(DistanceSquared);
        const float DirectionX = DeltaX / Distance;
        const float DirectionZ = DeltaZ / Distance;
        const float LiftY = StartY - Config.MaxStepHeight - Config.CollisionSkin;
        FGameWorldCollisionHit LiftHit{};
        if (VerticalPathClear(StartX, StartZ, StartY, LiftY, &LiftHit) && !CapsuleOverlapsStatic(X, LiftY, Z, &LiftHit, true))
        {
            bool FoundStep = false;
            float StepY = StartY;
            float BestRise = (std::numeric_limits<float>::max)();
            const float ProbeRadius = (std::max)(Config.PlayerCollisionRadius - Config.CollisionSkin, 0.05f);
            for (int ProbeIndex = 1; ProbeIndex <= 4; ++ProbeIndex)
            {
                const float ProbeDistance = ProbeRadius * (0.25f + static_cast<float>(ProbeIndex) * 0.22f);
                const float ProbeX = X + DirectionX * ProbeDistance;
                const float ProbeZ = Z + DirectionZ * ProbeDistance;
                float CandidateY = StartY;
                FVector3 CandidateNormal{};
                if (!SupportHeightAt(ProbeX, ProbeZ, StartY, CandidateY, &CandidateNormal) || std::abs(CandidateNormal.Y) < Config.CollisionFloorNormalThreshold)
                {
                    continue;
                }
                const float Rise = StartY - CandidateY;
                if (Rise < -Config.CollisionSkin || Rise > Config.MaxStepHeight + Config.CollisionSkin || Rise >= BestRise)
                {
                    continue;
                }
                FGameWorldCollisionHit StepHit{};
                if (TerrainPenetrates(CandidateY, false, &StepHit) || CapsuleOverlapsStatic(X, CandidateY, Z, &StepHit, true))
                {
                    RememberHit(StepHit);
                    continue;
                }
                FoundStep = true;
                StepY = CandidateY;
                BestRise = Rise;
            }
            if (FoundStep)
            {
                return CommitGrounded(StepY);
            }
        }
        else
        {
            RememberHit(LiftHit);
        }
    }
    if (!HasSupport)
    {
        FGameWorldCollisionHit Hit{};
        if (!TerrainPenetrates(StartY, false, &Hit) && !CapsuleOverlapsStatic(X, StartY, Z, &Hit, true))
        {
            SpawnX = X;
            SpawnZ = Z;
            Grounded = false;
            VelocityY = (std::max)(VelocityY, 0.0f);
            return true;
        }
        RememberHit(Hit);
    }
    if (OutHit)
    {
        *OutHit = BlockingHit;
    }
    return false;
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
    float TerrainY = 0.0f;
    FVector3 Normal{};
    if (!TerrainSurfaceNearAt(SpawnX, SpawnZ, SpawnY, TerrainY, Normal))
    {
        return;
    }
    const float NormalY = std::abs(Normal.Y);
    if (NormalY >= Config.SlopeSlideNormalY || NormalY <= 0.0001f)
    {
        return;
    }
    const float DownhillX = -Normal.X / NormalY;
    const float DownhillZ = -Normal.Z / NormalY;
    const float HorizontalLength = std::sqrt(DownhillX * DownhillX + DownhillZ * DownhillZ);
    if (HorizontalLength <= 0.0001f)
    {
        return;
    }
    const float SlopeStrength = std::sqrt((std::max)(0.0f, 1.0f - NormalY * NormalY));
    const float Distance = Config.JumpGravity * SlopeStrength * Config.SlopeSlideFactor * DeltaSeconds;
    TryMoveTo(SpawnX + DownhillX / HorizontalLength * Distance, SpawnZ + DownhillZ / HorizontalLength * Distance);
}

void FD3D9GameWorldScene::Impl::UpdateVertical(float DeltaSeconds)
{
    if (DeltaSeconds <= 0.0f)
    {
        return;
    }
    if (Grounded)
    {
        float GroundY = SpawnY;
        if (SupportHeightAt(SpawnX, SpawnZ, SpawnY, GroundY))
        {
            if (!CapsuleOverlapsStatic(SpawnX, GroundY, SpawnZ, nullptr, true))
            {
                SpawnY = GroundY;
                VelocityY = 0.0f;
                return;
            }
            if (!CapsuleOverlapsStatic(SpawnX, SpawnY, SpawnZ, nullptr, true))
            {
                VelocityY = 0.0f;
                return;
            }
        }
        Grounded = false;
    }
    const float PreviousY = SpawnY;
    const float NewVelocityY = VelocityY + Config.JumpGravity * DeltaSeconds;
    const float TargetY = SpawnY + (VelocityY + NewVelocityY) * 0.5f * DeltaSeconds;
    if (NewVelocityY < 0.0f)
    {
        const float Distance = std::abs(TargetY - PreviousY);
        const float StepSize = (std::max)(0.05f, (std::min)(Config.MovementCollisionStep, Config.PlayerCollisionRadius * 0.5f));
        const int StepCount = (std::max)(1, static_cast<int>(std::ceil(Distance / StepSize)));
        float SafeY = PreviousY;
        for (int Step = 1; Step <= StepCount; ++Step)
        {
            const float CandidateY = PreviousY + (TargetY - PreviousY) * static_cast<float>(Step) / static_cast<float>(StepCount);
            if (!CapsuleOverlapsStatic(SpawnX, CandidateY, SpawnZ, nullptr, true))
            {
                SafeY = CandidateY;
                continue;
            }
            float Low = SafeY;
            float High = CandidateY;
            for (int Iteration = 0; Iteration < 6; ++Iteration)
            {
                const float Middle = (Low + High) * 0.5f;
                if (CapsuleOverlapsStatic(SpawnX, Middle, SpawnZ, nullptr, true))
                {
                    High = Middle;
                }
                else
                {
                    Low = Middle;
                }
            }
            SpawnY = Low;
            VelocityY = 0.0f;
            return;
        }
        SpawnY = TargetY;
        VelocityY = NewVelocityY;
        return;
    }
    bool FoundFloor = false;
    float FloorY = TargetY;
    float TerrainY = 0.0f;
    FVector3 TerrainNormal{};
    if (TerrainSurfaceNearAt(SpawnX, SpawnZ, PreviousY, TerrainY, TerrainNormal) && PreviousY > TerrainY + Config.CollisionSkin)
    {
        SpawnY = TerrainY;
        VelocityY = 0.0f;
        Grounded = true;
        return;
    }
    const float MinFloorY = PreviousY - Config.CollisionSkin;
    const float MaxFloorY = TargetY + Config.CollisionSkin;
    if (TerrainSurfaceNearAt(SpawnX, SpawnZ, PreviousY, TerrainY, TerrainNormal) && TerrainY >= MinFloorY && TerrainY <= MaxFloorY)
    {
        FoundFloor = true;
        FloorY = TerrainY;
    }
    float StaticY = 0.0f;
    if (StaticFloorHeightAt(SpawnX, SpawnZ, MinFloorY, MaxFloorY, StaticY) && (!FoundFloor || StaticY < FloorY))
    {
        FoundFloor = true;
        FloorY = StaticY;
    }
    if (FoundFloor && !CapsuleOverlapsStatic(SpawnX, FloorY, SpawnZ, nullptr, true))
    {
        SpawnY = FloorY;
        VelocityY = 0.0f;
        Grounded = true;
        return;
    }
    const float Distance = TargetY - PreviousY;
    const float StepSize = (std::max)(0.05f, (std::min)(Config.MovementCollisionStep, Config.PlayerCollisionRadius * 0.5f));
    const int StepCount = (std::max)(1, static_cast<int>(std::ceil(std::abs(Distance) / StepSize)));
    float SafeY = PreviousY;
    for (int Step = 1; Step <= StepCount; ++Step)
    {
        const float CandidateY = PreviousY + Distance * static_cast<float>(Step) / static_cast<float>(StepCount);
        FGameWorldCollisionHit Hit{};
        if (!CapsuleOverlapsStatic(SpawnX, CandidateY, SpawnZ, &Hit, false))
        {
            SafeY = CandidateY;
            continue;
        }
        float Low = SafeY;
        float High = CandidateY;
        for (int Iteration = 0; Iteration < 6; ++Iteration)
        {
            const float Middle = (Low + High) * 0.5f;
            if (CapsuleOverlapsStatic(SpawnX, Middle, SpawnZ, nullptr, false))
            {
                High = Middle;
            }
            else
            {
                Low = Middle;
            }
        }
        SpawnY = Low;
        VelocityY = 0.0f;
        Grounded = Hit.Normal.Y <= -Config.CollisionFloorNormalThreshold;
        if (Grounded)
        {
            float ContactY = SpawnY;
            if (SupportHeightAt(SpawnX, SpawnZ, SpawnY, ContactY) && !CapsuleOverlapsStatic(SpawnX, ContactY, SpawnZ, nullptr, true))
            {
                SpawnY = ContactY;
            }
        }
        return;
    }
    SpawnY = TargetY;
    VelocityY = NewVelocityY;
}

void FD3D9GameWorldScene::Impl::UpdateWeather(float DeltaSeconds)
{
    if (!Config.WeatherEnabled || WeatherScenarios.empty() || WeatherSequence.empty())
    {
        WeatherTransitionBlend = 0.0f;
        WeatherRain = 0.0f;
        WeatherCloudCover = 0.0f;
        WeatherFog = 0.0f;
        WeatherWind = 0.35f;
        WeatherSkyScrollScale = 1.0f;
        ApplyWeatherEnvironment();
        return;
    }

    WeatherScenarioElapsed += (std::max)(DeltaSeconds, 0.0f);
    bool scenarioChanged = false;
    for (;;)
    {
        const std::size_t sequenceIndex = WeatherSequencePosition % WeatherSequence.size();
        const float duration = WeatherScenarios[WeatherSequence[sequenceIndex]].Duration;
        if (WeatherScenarioElapsed < duration || duration <= 0.0f)
        {
            break;
        }
        WeatherScenarioElapsed -= duration;
        WeatherSequencePosition = (WeatherSequencePosition + 1) % WeatherSequence.size();
        scenarioChanged = true;
    }
    if (scenarioChanged)
    {
        RefreshWeatherSkyTextures();
    }

    struct FSample
    {
        float Rain = 0.0f;
        float Cloud = 0.0f;
        float Fog = 0.0f;
        float Wind = 0.35f;
        float Scroll = 1.0f;
    };
    auto sampleScenario = [](const WeatherScenario& scenario, float time)
    {
        FSample sample;
        sample.Rain = scenario.Rain;
        sample.Cloud = scenario.CloudCover;
        sample.Fog = scenario.Fog;
        sample.Wind = scenario.Wind;
        sample.Scroll = scenario.SkyScrollScale;
        if (scenario.Keyframes.empty())
        {
            return sample;
        }
        const WeatherKeyframe* from = &scenario.Keyframes.front();
        const WeatherKeyframe* to = from;
        for (const auto& frame : scenario.Keyframes)
        {
            if (frame.Time <= time)
            {
                from = &frame;
                to = &frame;
                continue;
            }
            to = &frame;
            break;
        }
        float blend = 0.0f;
        if (to != from && to->Time > from->Time)
        {
            blend = std::clamp((time - from->Time) / (to->Time - from->Time), 0.0f, 1.0f);
        }
        auto mix = [blend](float left, float right) { return left + (right - left) * blend; };
        sample.Rain = mix(from->Rain, to->Rain);
        sample.Fog = (std::max)(sample.Rain * 0.65f, mix(from->Fog, to->Fog));
        sample.Wind = (std::max)(0.35f + sample.Rain * 0.65f, mix(from->Wind, to->Wind));
        const float fromCloud = from->Cloud >= 0.0f ? from->Cloud : scenario.CloudCover;
        const float toCloud = to->Cloud >= 0.0f ? to->Cloud : scenario.CloudCover;
        sample.Cloud = (std::max)(mix(fromCloud, toCloud), sample.Rain * 0.9f);
        return sample;
    };

    const std::size_t currentSequence = WeatherSequencePosition % WeatherSequence.size();
    const std::size_t nextSequence = (currentSequence + 1) % WeatherSequence.size();
    const auto& currentScenario = WeatherScenarios[WeatherSequence[currentSequence]];
    const auto& nextScenario = WeatherScenarios[WeatherSequence[nextSequence]];
    const float transitionDuration = (std::min)(Config.WeatherTransitionSeconds, currentScenario.Duration * 0.3f);
    const float transitionStart = currentScenario.Duration - transitionDuration;
    WeatherTransitionBlend = transitionDuration > 0.0f ? std::clamp((WeatherScenarioElapsed - transitionStart) / transitionDuration, 0.0f, 1.0f) : 0.0f;
    const FSample current = sampleScenario(currentScenario, WeatherScenarioElapsed);
    const FSample next = sampleScenario(nextScenario, 0.0f);
    auto mix = [this](float left, float right) { return left + (right - left) * WeatherTransitionBlend; };
    WeatherRain = std::clamp(mix(current.Rain, next.Rain), 0.0f, 1.0f);
    WeatherCloudCover = std::clamp(mix(current.Cloud, next.Cloud), 0.0f, 1.0f);
    WeatherFog = std::clamp(mix(current.Fog, next.Fog), 0.0f, 1.0f);
    WeatherWind = std::clamp(mix(current.Wind, next.Wind), 0.15f, 2.0f);
    WeatherSkyScrollScale = std::clamp(mix(current.Scroll, next.Scroll), 0.0f, 4.0f);
    ApplyWeatherEnvironment();
}

bool FD3D9GameWorldScene::Impl::Update(float DeltaSeconds, const FGameMovementInput& Input, std::wstring& Error)
{
    if (!Initialized)
    {
        Error = L"game world scene is not initialized";
        return false;
    }
    DeltaSeconds = std::clamp(DeltaSeconds, 0.0f, (std::max)(0.01f, Config.MaxMovementDeltaSeconds));
    ElapsedSeconds += DeltaSeconds;
    SetGameTime(GameTimeFraction + DeltaSeconds * 12.0f / 86400.0f);
    UpdateWeather(DeltaSeconds);
    const float Forward = (Input.Forward ? 1.0f : 0.0f) - (Input.Backward ? 1.0f : 0.0f);
    const float Right = (Input.StrafeRight ? 1.0f : 0.0f) - (Input.StrafeLeft ? 1.0f : 0.0f);
    const float InputLength = std::sqrt(Forward * Forward + Right * Right);
    const bool Moving = InputLength > 0.0001f && DeltaSeconds > 0.0f;
    UpdatePlayerAnimation(DeltaSeconds, Moving, Input.Run);
    UpdateRemotePlayerAnimations(DeltaSeconds);
    UpdateNpcAnimation(DeltaSeconds);
    SpawnAngle = -CameraYaw;
    const int CollisionCenterRow = static_cast<int>(std::floor(SpawnX / Config.TileSize)) + Config.OriginRow;
    const int CollisionCenterColumn = Config.OriginColumn - static_cast<int>(std::floor(SpawnZ / Config.TileSize));
    if (CollisionCenterRow != TerrainCenterRow || CollisionCenterColumn != TerrainCenterColumn)
    {
        try
        {
            LoadVisibleTerrain();
            LoadVisibleStaticObjects();
        }
        catch (const std::exception& Exception)
        {
            AssignError(Error, std::string("game world collision streaming failed: ") + Exception.what());
            return false;
        }
    }
    if (PlayerCollisionNeedsRecovery)
    {
        RecoverFromPenetration();
    }
    if (Moving)
    {
        const float NormalizedForward = Forward / InputLength;
        const float NormalizedRight = Right / InputLength;
        const float Speed = Config.WalkSpeed * (Input.Run ? Config.RunMultiplier : 1.0f);
        VelocityX = (std::sin(CameraYaw) * NormalizedForward + std::cos(CameraYaw) * NormalizedRight) * Speed;
        VelocityZ = (std::cos(CameraYaw) * NormalizedForward - std::sin(CameraYaw) * NormalizedRight) * Speed;
        const float DisplacementX = VelocityX * DeltaSeconds;
        const float DisplacementZ = VelocityZ * DeltaSeconds;
        const float Distance = std::sqrt(DisplacementX * DisplacementX + DisplacementZ * DisplacementZ);
        const float CollisionStep = (std::max)(0.05f, (std::min)(Config.MovementCollisionStep, Config.PlayerCollisionRadius * 0.5f));
        const int DistanceStepCount = (std::max)(1, static_cast<int>(std::ceil(Distance / CollisionStep)));
        const float SimulationStepSeconds = (std::max)(0.005f, Config.MaxSimulationStepSeconds);
        const int TimeStepCount = (std::max)(1, static_cast<int>(std::ceil(DeltaSeconds / SimulationStepSeconds)));
        const int StepCount = (std::max)(DistanceStepCount, TimeStepCount);
        const float StepX = DisplacementX / static_cast<float>(StepCount);
        const float StepZ = DisplacementZ / static_cast<float>(StepCount);
        const float StepSeconds = DeltaSeconds / static_cast<float>(StepCount);
        for (int Step = 0; Step < StepCount; ++Step)
        {
            const float StartX = SpawnX;
            const float StartZ = SpawnZ;
            FGameWorldCollisionHit Hit;
            bool Moved = TryMoveTo(StartX + StepX, StartZ + StepZ, &Hit);
            if (!Moved)
            {
                float NormalX = Hit.Normal.X;
                float NormalZ = Hit.Normal.Z;
                const float NormalLength = std::sqrt(NormalX * NormalX + NormalZ * NormalZ);
                if (NormalLength > 0.0001f)
                {
                    NormalX /= NormalLength;
                    NormalZ /= NormalLength;
                    if (NormalX * StepX + NormalZ * StepZ > 0.0f)
                    {
                        NormalX = -NormalX;
                        NormalZ = -NormalZ;
                    }
                    const float IntoSurface = StepX * NormalX + StepZ * NormalZ;
                    const float SlideX = StepX - NormalX * (std::min)(IntoSurface, 0.0f);
                    const float SlideZ = StepZ - NormalZ * (std::min)(IntoSurface, 0.0f);
                    Moved = SlideX * SlideX + SlideZ * SlideZ > 0.0000001f && TryMoveTo(StartX + SlideX, StartZ + SlideZ);
                }
            }
            if (!Moved)
            {
                const bool MoveXFirst = std::abs(StepX) >= std::abs(StepZ);
                if (MoveXFirst)
                {
                    Moved = TryMoveTo(StartX + StepX, StartZ);
                    if (Moved)
                    {
                        TryMoveTo(SpawnX, SpawnZ + StepZ);
                    }
                    else
                    {
                        Moved = TryMoveTo(StartX, StartZ + StepZ);
                    }
                }
                else
                {
                    Moved = TryMoveTo(StartX, StartZ + StepZ);
                    if (Moved)
                    {
                        TryMoveTo(SpawnX + StepX, SpawnZ);
                    }
                    else
                    {
                        Moved = TryMoveTo(StartX + StepX, StartZ);
                    }
                }
            }
            UpdateVertical(StepSeconds);
        }
    }
    else
    {
        VelocityX = 0.0f;
        VelocityZ = 0.0f;
        const float SimulationStepSeconds = (std::max)(0.005f, Config.MaxSimulationStepSeconds);
        const int StepCount = (std::max)(1, static_cast<int>(std::ceil(DeltaSeconds / SimulationStepSeconds)));
        const float StepSeconds = DeltaSeconds / static_cast<float>(StepCount);
        for (int Step = 0; Step < StepCount; ++Step)
        {
            ApplySlopeSlide(StepSeconds);
            UpdateVertical(StepSeconds);
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
            LoadVisibleTerrain();
            LoadVisibleStaticObjects();
            RecoverFromPenetration();
            if (Config.GrassQuality > 0)
            {
                LoadVisibleGrass();
                GrassLoadedThisFrame = true;
            }
            StreamingUpdatedThisFrame = true;
        }
        catch (const std::exception& Exception)
        {
            AssignError(Error, std::string("game world terrain update failed: ") + Exception.what());
            return false;
        }
    }
    const float GrassDx = SpawnX - GrassAnchorX;
    const float GrassDz = SpawnZ - GrassAnchorZ;
    if (Config.GrassQuality > 0 && !GrassLoadedThisFrame && (GrassRefreshIncomplete || !GrassAnchorValid || GrassDx * GrassDx + GrassDz * GrassDz >= Config.GrassGenerationMargin * Config.GrassGenerationMargin))
    {
        try
        {
            LoadVisibleGrass();
            GrassLoadedThisFrame = true;
        }
        catch (const std::exception& Exception)
        {
            if (Logger)
            {
                Logger->Warning(std::string("game world grass update skipped: ") + Exception.what());
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
    CameraPitch = std::clamp(CameraPitch - MouseDy * Config.CameraPitchSpeed, Config.CameraMinPitch, Config.CameraMaxPitch);
    SpawnAngle = -CameraYaw;
}
