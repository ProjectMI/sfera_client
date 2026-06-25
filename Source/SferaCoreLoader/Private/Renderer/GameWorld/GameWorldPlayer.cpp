#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

void FD3D9GameWorldScene::Impl::SkinPlayerFrame()
{
    if (!PlayerModel.IsValid() || !PlayerVertexBuffer)
    {
        return;
    }
    const std::size_t action = PlayerAction < PlayerModel.ActionCount()
    ? PlayerAction
    : kPlayerIdleAction;
    const std::size_t ActionStart = PlayerModel.ActionFrameStart(action);
    const std::size_t ActionFrames = PlayerModel.ActionFrameCount(action);
    if (ActionFrames == 0)
    {
        return;
    }
    const float FramePosition = PlayerAnimTime / kPlayerAnimSecondsPerFrame;
    const std::size_t LocalFrame = static_cast<std::size_t>(std::floor(FramePosition)) % ActionFrames;
    const std::size_t NextLocalFrame = (LocalFrame + 1) % ActionFrames;
    const float FrameAlpha = FramePosition - std::floor(FramePosition);
    const std::size_t frame = ActionStart + LocalFrame;
    const std::size_t nextFrame = ActionStart + NextLocalFrame;

    try
    {
        SkinFrameInterpolated(PlayerModel, frame, nextFrame, FrameAlpha, PlayerSkinScratch);
    } catch (...)
    {
        return;
    }

    const std::size_t VertexCount = PlayerSkinScratch.size() / 8;
    PlayerVertexScratch.resize(VertexCount);
    float CrownWorldY = 0.0f; // smallest y == highest point (world +y down)
    for (std::size_t i = 0; i < VertexCount; ++i)
    {
        const float* s = PlayerSkinScratch.data() + i * 8;
        const float wy = -s[1];
        if (i == 0 || wy < CrownWorldY)
        {
            CrownWorldY = wy;
        }
        PlayerVertexScratch[i] = WorldVertex{
            s[0], wy, s[2],
            s[3], -s[4], s[5],
            0xffffffff,
            s[6], s[7],
            s[6], s[7],
        };
    }

    const UINT VertexBytes = static_cast<UINT>(VertexCount * sizeof(WorldVertex));
    void* VertexData = nullptr;
    if (SUCCEEDED(PlayerVertexBuffer->Lock(0, VertexBytes, &VertexData, 0)))
    {
        CopyVectorBytes(VertexData, PlayerVertexScratch, VertexBytes);
        PlayerVertexBuffer->Unlock();
    }

    PlayerLiveCrownY = CrownWorldY;
    if (!PlayerEyeInitialized)
    {
        PlayerLockedCrownY = CrownWorldY;
        PlayerEyeLocalX = 0.0f;
        PlayerEyeLocalY = CrownWorldY + kEyeBelowCrownWorld;
        PlayerEyeLocalZ = kEyeForwardModel;
        PlayerEyeValid = true;
        PlayerEyeInitialized = true;
    }
    PlayerLastSkinnedFrame = frame;
}

void FD3D9GameWorldScene::Impl::LoadPlayerModel(const FSkinnedCharacterModel& model)
{
    if (!model.IsValid())
    {
        throw std::runtime_error("selected player skinned model is empty");
    }
    PlayerModel = model;
    PlayerHeadBone = PlayerModel.BoneIndex("head1");
    if (PlayerHeadBone < 0)
    {
        PlayerHeadBone = PlayerModel.BoneIndex("head");
    }
    PlayerAction = kPlayerIdleAction;
    PlayerLastSkinnedFrame = (std::numeric_limits<std::size_t>::max)();
    PlayerAnimTime = 0.0f;

    for (const auto& source : PlayerModel.Batches)
    {
        if (source.IndexCount < 3 ||
        source.StartIndex > PlayerModel.Indices.size() ||
        source.IndexCount > PlayerModel.Indices.size() - source.StartIndex)
        {
            throw std::runtime_error("selected player model contains an invalid material batch");
        }
        std::filesystem::path TexturePath = source.TexturePath;
        if (!std::filesystem::exists(TexturePath))
        {
            TexturePath = ResolveOptionalPath(TexturePath.generic_string());
        }
        if (TexturePath.empty() || !std::filesystem::exists(TexturePath))
        {
            throw std::runtime_error("selected player texture is missing: " + source.TexturePath.string());
        }
        FSceneBatch batch;
        batch.StartIndex = source.StartIndex;
        batch.IndexCount = source.IndexCount;
        batch.Texture = LoadCachedDdsTexture(TexturePath);
        batch.Head = source.IsHead;
        PlayerBatches.push_back(batch);
    }

    PlayerVertexCount = static_cast<UINT>(PlayerModel.Sources.size());
    PlayerVertexScratch.assign(PlayerModel.Sources.size(), WorldVertex{});
    PlayerVertexBuffer = CreateManagedVertexBufferOrThrow(Device, PlayerVertexScratch, kWorldVertexFvf, "CreateVertexBuffer player");
    PlayerIndexBuffer = CreateManagedIndexBufferOrThrow(Device, PlayerModel.Indices, D3DFMT_INDEX16, "CreateIndexBuffer player");

    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::UpdatePlayerAnimation(float DeltaSeconds, bool moving, bool running)
{
    if (!PlayerModel.IsValid())
    {
        return;
    }
    const auto resolveAction = [&](int action) -> std::size_t
    {
        if (action < 0)
        {
            return kPlayerIdleAction;
        }
        const auto resolved = static_cast<std::size_t>(action);
        return resolved < PlayerModel.ActionCount() && PlayerModel.ActionFrameCount(resolved) > 0 ? resolved : kPlayerIdleAction;
    };
    const std::size_t desired = !moving
    ? resolveAction(PlayerModel.AnimIdle)
    : (running ? resolveAction(PlayerModel.AnimRun)
    : resolveAction(PlayerModel.AnimWalk));
    if (desired != PlayerAction)
    {
        PlayerAction = desired;
        PlayerAnimTime = 0.0f; // restart the cycle when the action changes
    }
    PlayerAnimTime += (std::max)(0.0f, DeltaSeconds);

    PlayerBodyShift = !moving ? kIdleBodyBackShift : (running ? kRunBodyBackShift : kWalkBodyBackShift);
    PlayerWalking = moving;

    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::DrawPlayer()
{
    if (!PlayerVertexBuffer || !PlayerIndexBuffer || PlayerBatches.empty())
    {
        return;
    }
    auto world = RotationYMatrix(CameraYaw);
    world._41 = SpawnX - std::sin(CameraYaw) * PlayerBodyShift;
    world._42 = SpawnY;
    world._43 = SpawnZ - std::cos(CameraYaw) * PlayerBodyShift;
    const bool UseShader = WorldShadersReady;
    if (UseShader)
    {
        BeginBaseShader();
        SetBaseLightConstants();
        SetBaseWorld(world);
    } else
    {
        Device->SetTransform(D3DTS_WORLD, &world);
        Device->SetFVF(kWorldVertexFvf);
    }
    Device->SetStreamSource(0, PlayerVertexBuffer, 0, sizeof(WorldVertex));
    Device->SetIndices(PlayerIndexBuffer);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    for (const auto& batch : PlayerBatches)
    {
        if (batch.Head)
        {
            continue;
        }

        Device->SetTexture(0, batch.Texture);
        const UINT triangleCount = batch.IndexCount / 3;
        Device->DrawIndexedPrimitive(
        D3DPT_TRIANGLELIST,
        0,
        0,
        PlayerVertexCount,
        batch.StartIndex,
        triangleCount);
        RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Player);
    }
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    if (UseShader)
    {
        EndBaseShader();
    }
}
