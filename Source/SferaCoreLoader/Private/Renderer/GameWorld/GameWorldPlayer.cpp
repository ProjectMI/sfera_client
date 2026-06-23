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
    const std::size_t LocalFrame =
    static_cast<std::size_t>(PlayerAnimTime / kPlayerAnimSecondsPerFrame) % ActionFrames;
    const std::size_t frame = ActionStart + LocalFrame;

    try
    {
        SkinFrame(PlayerModel, frame, PlayerSkinScratch);
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
        // SkinFrame emits character-select space (+y up); the world uses
        // +y down, so flip y on both Position and normal, matching the
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

    // Track the head-top each frame; the eye rides just below it.
    PlayerLiveCrownY = CrownWorldY;
    // Lock the baseline eye height to the first skinned frame so idle body
    // motion (breathing) does not bob the camera. The walking up/down bob is
    // added in UpdateViewProjection from the live head-top deviation.
    if (!PlayerEyeInitialized)
    {
        PlayerLockedCrownY = CrownWorldY;
        PlayerEyeLocalX = 0.0f;
        PlayerEyeLocalY = CrownWorldY + kEyeBelowCrownWorld;
        PlayerEyeLocalZ = kEyeForwardModel;
        PlayerEyeValid = true;
        PlayerEyeInitialized = true;
    }
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
    const UINT VertexBytes = static_cast<UINT>(PlayerModel.Sources.size() * sizeof(WorldVertex));
    HRESULT hr = Device->CreateVertexBuffer(
    VertexBytes,
    D3DUSAGE_WRITEONLY,
    kWorldVertexFvf,
    D3DPOOL_MANAGED,
    &PlayerVertexBuffer,
    nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateVertexBuffer player", hr));
    }

    const UINT IndexBytes = static_cast<UINT>(PlayerModel.Indices.size() * sizeof(uint16));
    hr = Device->CreateIndexBuffer(
    IndexBytes,
    D3DUSAGE_WRITEONLY,
    D3DFMT_INDEX16,
    D3DPOOL_MANAGED,
    &PlayerIndexBuffer,
    nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("CreateIndexBuffer player", hr));
    }
    void* IndexData = nullptr;
    hr = PlayerIndexBuffer->Lock(0, IndexBytes, &IndexData, 0);
    if (FAILED(hr))
    {
        throw std::runtime_error(HResultTextNarrow("PlayerIndexBuffer::Lock", hr));
    }
    CopyVectorBytes(IndexData, PlayerModel.Indices, IndexBytes);
    PlayerIndexBuffer->Unlock();

    // Skin the first frame so the body and eye height are IsValid immediately.
    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::UpdatePlayerAnimation(float DeltaSeconds, bool moving, bool running)
{
    if (!PlayerModel.IsValid())
    {
        return;
    }
    // Action by movement state, using SKL Indices carried on the model
    // per-model params reader is wired these default to idle.
    const std::size_t desired = !moving
    ? static_cast<std::size_t>(PlayerModel.AnimIdle)
    : (running ? static_cast<std::size_t>(PlayerModel.AnimRun)
    : static_cast<std::size_t>(PlayerModel.AnimWalk));
    if (desired != PlayerAction)
    {
        PlayerAction = desired;
        PlayerAnimTime = 0.0f; // restart the cycle when the action changes
    }
    PlayerAnimTime += (std::max)(0.0f, DeltaSeconds);

    // Snap the body's backward offset on/off with movement (no easing).
    PlayerBodyShift = moving ? kMoveBodyBackShift : 0.0f;
    PlayerWalking = moving;

    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::DrawPlayer()
{
    if (!PlayerVertexBuffer || !PlayerIndexBuffer || PlayerBatches.empty())
    {
        return;
    }
    // The body faces where the camera looks, so in first person it stays
    // locked below the view as you turn (camera rides inside the head).
    auto world = RotationYMatrix(CameraYaw);
    // Ease the body backward along the look axis while moving so the
    // forward-leaning torso's neck cut never enters the lens.
    world._41 = SpawnX - std::sin(CameraYaw) * PlayerBodyShift;
    world._42 = SpawnY;
    world._43 = SpawnZ - std::cos(CameraYaw) * PlayerBodyShift;
    const bool UseShader = WorldShadersReady;
    if (UseShader)
    {
        // The body is already skinned to world-posed vertices on the CPU, so
        // the base lit+textured shader applies like any static object.
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
    // Backface-cull the body so looking down does not reveal the dark
    // interior of the torso/neck. The rest of the world draws double-sided.
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    for (const auto& batch : PlayerBatches)
    {
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
