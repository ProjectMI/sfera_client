#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"
#include "Renderer/D3D9CharacterScene.h"

namespace
{
std::size_t ResolvePlayerAction(const FSkinnedCharacterModel& model, int action)
{
    if (action < 0) { return kPlayerIdleAction; }
    const auto resolved = static_cast<std::size_t>(action);
    return resolved < model.ActionCount() && model.ActionFrameCount(resolved) > 0 ? resolved : kPlayerIdleAction;
}

float RemoteAnimationSeed(uint64 entityId)
{
    uint64 value = entityId + 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    value ^= value >> 31;
    return static_cast<float>(value & 0xffffu) / 65535.0f;
}

FCharacterCreationAppearance DefaultRemoteAppearance()
{
    return FCharacterCreationAppearance{};
}

bool SameAppearance(const FCharacterCreationAppearance& left, const FCharacterCreationAppearance& right)
{
    return left.Female == right.Female && left.ModelBase == right.ModelBase && left.Face == right.Face && left.Hair == right.Hair && left.HairColor == right.HairColor && left.Tattoo == right.Tattoo;
}

uint64 RemoteAppearanceKey(const FCharacterCreationAppearance& appearance)
{
    const auto byte = [](int32 value) { return static_cast<uint64>(std::clamp(value, 0, 255)); };
    uint64 key = appearance.Female ? 1ull : 0ull;
    key |= byte(appearance.Face) << 8;
    key |= byte(appearance.Hair) << 16;
    key |= byte(appearance.HairColor) << 24;
    key |= byte(appearance.Tattoo) << 32;
    key |= byte(appearance.ModelBase) << 40;
    return key + 1;
}

bool BuildCharacterPose(const FSkinnedCharacterModel& model, std::size_t action, float animationTime, std::vector<float>& skinScratch, std::vector<WorldVertex>& vertexScratch, float* crownWorldY)
{
    if (!model.IsValid()) { return false; }
    action = action < model.ActionCount() ? action : kPlayerIdleAction;
    const std::size_t actionStart = model.ActionFrameStart(action);
    const std::size_t actionFrames = model.ActionFrameCount(action);
    if (actionFrames == 0) { return false; }
    const float framePosition = animationTime / kPlayerAnimSecondsPerFrame;
    const std::size_t localFrame = static_cast<std::size_t>(std::floor(framePosition)) % actionFrames;
    const std::size_t nextLocalFrame = (localFrame + 1) % actionFrames;
    const float frameAlpha = framePosition - std::floor(framePosition);
    try { SkinFrameInterpolated(model, actionStart + localFrame, actionStart + nextLocalFrame, frameAlpha, skinScratch); }
    catch (...) { return false; }
    const std::size_t vertexCount = skinScratch.size() / 8;
    vertexScratch.resize(vertexCount);
    float crown = 0.0f;
    for (std::size_t index = 0; index < vertexCount; ++index)
    {
        const float* source = skinScratch.data() + index * 8;
        const float worldY = -source[1];
        if (index == 0 || worldY < crown) { crown = worldY; }
        vertexScratch[index] = WorldVertex{source[0], worldY, source[2], source[3], -source[4], source[5], 0xffffffff, source[6], source[7], source[6], source[7]};
    }
    if (crownWorldY) { *crownWorldY = crown; }
    return true;
}

bool UploadCharacterPose(IDirect3DVertexBuffer9* vertexBuffer, const std::vector<WorldVertex>& vertices)
{
    if (!vertexBuffer || vertices.empty()) { return false; }
    const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    void* vertexData = nullptr;
    if (FAILED(vertexBuffer->Lock(0, vertexBytes, &vertexData, 0))) { return false; }
    CopyVectorBytes(vertexData, vertices, vertexBytes);
    vertexBuffer->Unlock();
    return true;
}
}

std::vector<FSceneBatch> FD3D9GameWorldScene::Impl::LoadCharacterBatches(const FSkinnedCharacterModel& model)
{
    std::vector<FSceneBatch> batches;
    try
    {
        batches.reserve(model.Batches.size());
        for (const auto& source : model.Batches)
        {
            if (source.IndexCount < 3 || source.StartIndex > model.Indices.size() || source.IndexCount > model.Indices.size() - source.StartIndex) { throw std::runtime_error("character model contains an invalid material batch"); }
            std::filesystem::path texturePath = source.TexturePath;
            if (!std::filesystem::exists(texturePath)) { texturePath = ResolveOptionalPath(texturePath.generic_string()); }
            if (texturePath.empty() || !std::filesystem::exists(texturePath)) { throw std::runtime_error("character texture is missing: " + source.TexturePath.string()); }
            batches.push_back(FSceneBatch{source.StartIndex, source.IndexCount, {}, LoadCachedDdsTexture(texturePath), false, source.IsHead});
        }
        return batches;
    }
    catch (...)
    {
        for (auto& batch : batches) { SafeRelease(batch.Texture); }
        throw;
    }
}

FD3D9GameWorldScene::Impl::FRemotePlayerModelResource* FD3D9GameWorldScene::Impl::EnsureRemotePlayerModel(const FCharacterCreationAppearance& appearance)
{
    const uint64 key = RemoteAppearanceKey(appearance);
    if (auto iterator = RemotePlayerModels.find(key); iterator != RemotePlayerModels.end()) { return &iterator->second; }
    if (!AssetResources || !Device) { return nullptr; }
    std::string error;
    FSkinnedCharacterModel model = FD3D9CharacterScene::BuildSkinnedModel(*AssetResources, appearance, error);
    if (!model.IsValid())
    {
        if (Logger) { Logger->Warning("remote character model load failed: " + error); }
        return nullptr;
    }
    FRemotePlayerModelResource resource;
    try
    {
        resource.Model = std::move(model);
        resource.Batches = LoadCharacterBatches(resource.Model);
        resource.IndexBuffer = CreateManagedIndexBufferOrThrow(Device, resource.Model.Indices, D3DFMT_INDEX16, "CreateIndexBuffer remote player model");
        resource.VertexCount = static_cast<UINT>(resource.Model.Sources.size());
    }
    catch (const std::exception& exception)
    {
        for (auto& batch : resource.Batches) { SafeRelease(batch.Texture); }
        SafeRelease(resource.IndexBuffer);
        if (Logger) { Logger->Warning("remote character render resource creation failed: " + std::string(exception.what())); }
        return nullptr;
    }
    return &RemotePlayerModels.emplace(key, std::move(resource)).first->second;
}

void FD3D9GameWorldScene::Impl::SkinPlayerFrame()
{
    float crownWorldY = 0.0f;
    if (!BuildCharacterPose(PlayerModel, PlayerAction, PlayerAnimTime, PlayerSkinScratch, PlayerVertexScratch, &crownWorldY) || !UploadCharacterPose(PlayerVertexBuffer, PlayerVertexScratch)) { return; }
    PlayerLiveCrownY = crownWorldY;
    if (!PlayerEyeInitialized)
    {
        PlayerLockedCrownY = crownWorldY;
        PlayerEyeLocalX = 0.0f;
        PlayerEyeLocalY = crownWorldY + kEyeBelowCrownWorld;
        PlayerEyeLocalZ = 0.0f;
        PlayerEyeValid = true;
        PlayerEyeInitialized = true;
    }
    const std::size_t action = PlayerAction < PlayerModel.ActionCount() ? PlayerAction : kPlayerIdleAction;
    const std::size_t actionFrames = PlayerModel.ActionFrameCount(action);
    const std::size_t localFrame = actionFrames > 0 ? static_cast<std::size_t>(std::floor(PlayerAnimTime / kPlayerAnimSecondsPerFrame)) % actionFrames : 0;
    PlayerLastSkinnedFrame = PlayerModel.ActionFrameStart(action) + localFrame;
}

void FD3D9GameWorldScene::Impl::LoadPlayerModel(const FSkinnedCharacterModel& model)
{
    if (!model.IsValid()) { throw std::runtime_error("selected player skinned model is empty"); }
    PlayerModel = model;
    PlayerHeadBone = PlayerModel.BoneIndex("head1");
    if (PlayerHeadBone < 0) { PlayerHeadBone = PlayerModel.BoneIndex("head"); }
    PlayerAction = kPlayerIdleAction;
    PlayerLastSkinnedFrame = (std::numeric_limits<std::size_t>::max)();
    PlayerAnimTime = 0.0f;
    PlayerBatches = LoadCharacterBatches(PlayerModel);
    PlayerVertexCount = static_cast<UINT>(PlayerModel.Sources.size());
    PlayerVertexScratch.assign(PlayerModel.Sources.size(), WorldVertex{});
    PlayerVertexBuffer = CreateManagedVertexBufferOrThrow(Device, PlayerVertexScratch, kWorldVertexFvf, "CreateVertexBuffer player");
    PlayerIndexBuffer = CreateManagedIndexBufferOrThrow(Device, PlayerModel.Indices, D3DFMT_INDEX16, "CreateIndexBuffer player");
    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::UpdatePlayerAnimation(float deltaSeconds, bool moving, bool running)
{
    if (!PlayerModel.IsValid()) { return; }
    const std::size_t desired = !moving ? ResolvePlayerAction(PlayerModel, PlayerModel.AnimIdle) : ResolvePlayerAction(PlayerModel, running ? PlayerModel.AnimRun : PlayerModel.AnimWalk);
    if (desired != PlayerAction)
    {
        PlayerAction = desired;
        PlayerAnimTime = 0.0f;
    }
    PlayerAnimTime += (std::max)(0.0f, deltaSeconds);
    PlayerBodyShift = !moving ? kIdleBodyBackShift : (running ? kRunBodyBackShift : kWalkBodyBackShift);
    PlayerWalking = moving;
    SkinPlayerFrame();
}

void FD3D9GameWorldScene::Impl::UpdateRemotePlayerAnimations(float deltaSeconds)
{
    for (auto& [_, remote] : RemotePlayers)
    {
        const FCharacterCreationAppearance appearance = remote.Player.Appearance.value_or(DefaultRemoteAppearance());
        const uint64 modelKey = RemoteAppearanceKey(appearance);
        if (remote.ModelKey != modelKey)
        {
            SafeRelease(remote.VertexBuffer);
            remote.SkinScratch.clear();
            remote.VertexScratch.clear();
            remote.ModelKey = modelKey;
            remote.Action = kPlayerIdleAction;
        }
        FRemotePlayerModelResource* resource = EnsureRemotePlayerModel(appearance);
        if (!resource) { continue; }
        remote.MovementHold = (std::max)(0.0f, remote.MovementHold - deltaSeconds);
        const bool moving = remote.MovementHold > 0.0f;
        remote.Action = !moving ? ResolvePlayerAction(resource->Model, resource->Model.AnimIdle) : ResolvePlayerAction(resource->Model, remote.Running ? resource->Model.AnimRun : resource->Model.AnimWalk);
        remote.AnimationTime += (std::max)(0.0f, deltaSeconds);
        if (!remote.VertexBuffer)
        {
            try
            {
                remote.VertexScratch.assign(resource->Model.Sources.size(), WorldVertex{});
                remote.VertexBuffer = CreateManagedVertexBufferOrThrow(Device, remote.VertexScratch, kWorldVertexFvf, "CreateVertexBuffer remote player");
            }
            catch (const std::exception& exception)
            {
                if (Logger) { Logger->Warning("remote player vertex buffer creation failed: " + std::string(exception.what())); }
                continue;
            }
        }
        if (BuildCharacterPose(resource->Model, remote.Action, remote.AnimationTime, remote.SkinScratch, remote.VertexScratch, nullptr)) { UploadCharacterPose(remote.VertexBuffer, remote.VertexScratch); }
    }
}

void FD3D9GameWorldScene::Impl::DrawPlayer()
{
    if (!PlayerVertexBuffer || !PlayerIndexBuffer || PlayerBatches.empty()) { return; }
    const bool useShader = WorldShadersReady;
    if (useShader)
    {
        BeginBaseShader();
        SetBaseLightConstants();
    }
    else { Device->SetFVF(kWorldVertexFvf); }
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    auto drawModel = [&](IDirect3DVertexBuffer9* vertexBuffer, IDirect3DIndexBuffer9* indexBuffer, UINT vertexCount, const std::vector<FSceneBatch>& batches, const D3DMATRIX& world, bool includeHead)
    {
        if (!vertexBuffer || !indexBuffer || batches.empty()) { return; }
        Device->SetStreamSource(0, vertexBuffer, 0, sizeof(WorldVertex));
        Device->SetIndices(indexBuffer);
        if (useShader) { SetBaseWorld(world); }
        else { Device->SetTransform(D3DTS_WORLD, &world); }
        for (const auto& batch : batches)
        {
            if (batch.Head && !includeHead) { continue; }
            Device->SetTexture(0, batch.Texture);
            const UINT triangleCount = batch.IndexCount / 3;
            Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertexCount, batch.StartIndex, triangleCount);
            RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Player);
        }
    };
    for (const auto& [_, remote] : RemotePlayers)
    {
        const auto resource = RemotePlayerModels.find(remote.ModelKey);
        if (resource == RemotePlayerModels.end()) { continue; }
        auto remoteWorld = RotationYMatrix(static_cast<float>(-remote.Player.Position.Angle));
        remoteWorld._41 = static_cast<float>(remote.Player.Position.X);
        remoteWorld._42 = static_cast<float>(remote.Player.Position.Y);
        remoteWorld._43 = static_cast<float>(remote.Player.Position.Z);
        drawModel(remote.VertexBuffer, resource->second.IndexBuffer, resource->second.VertexCount, resource->second.Batches, remoteWorld, true);
    }
    auto world = RotationYMatrix(CameraYaw);
    const float visualBackShift = kPlayerVisualBackShift + PlayerBodyShift;
    world._41 = SpawnX - std::sin(CameraYaw) * visualBackShift;
    world._42 = SpawnY;
    world._43 = SpawnZ - std::cos(CameraYaw) * visualBackShift;
    drawModel(PlayerVertexBuffer, PlayerIndexBuffer, PlayerVertexCount, PlayerBatches, world, false);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    if (useShader) { EndBaseShader(); }
}

std::optional<FGameWorldPosition> FD3D9GameWorldScene::Impl::CurrentPlayerWorldPosition() const
{
    if (!Initialized) { return std::nullopt; }
    return FGameWorldPosition{SpawnX, SpawnY, SpawnZ, -CameraYaw};
}

void FD3D9GameWorldScene::Impl::UpsertRemotePlayer(const FRemoteGamePlayer& player)
{
    if (player.EntityId == 0 || !std::isfinite(player.Position.X) || !std::isfinite(player.Position.Y) || !std::isfinite(player.Position.Z) || !std::isfinite(player.Position.Angle)) { return; }
    auto [iterator, inserted] = RemotePlayers.try_emplace(player.EntityId);
    FRemotePlayerRenderState& target = iterator->second;
    if (inserted)
    {
        target.Player.EntityId = player.EntityId;
        target.AnimationTime = RemoteAnimationSeed(player.EntityId) * 8.0f;
        target.LastPacketTime = ElapsedSeconds;
    }
    else
    {
        const double deltaX = player.Position.X - target.Player.Position.X;
        const double deltaY = player.Position.Y - target.Player.Position.Y;
        const double deltaZ = player.Position.Z - target.Player.Position.Z;
        const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        const float packetDelta = target.LastPacketTime >= 0.0f ? ElapsedSeconds - target.LastPacketTime : 0.0f;
        if (distance > 0.01)
        {
            target.MovementHold = 0.3f;
            if (packetDelta > 0.01f)
            {
                const float speed = static_cast<float>(distance) / packetDelta;
                const float runThreshold = Config.WalkSpeed * (1.0f + Config.RunMultiplier) * 0.5f;
                target.Running = speed > runThreshold;
            }
        }
        target.LastPacketTime = ElapsedSeconds;
    }
    if (!player.Name.empty()) { target.Player.Name = player.Name; }
    if (player.Appearance)
    {
        const bool changed = !target.Player.Appearance || !SameAppearance(*target.Player.Appearance, *player.Appearance);
        target.Player.Appearance = player.Appearance;
        if (changed)
        {
            SafeRelease(target.VertexBuffer);
            target.SkinScratch.clear();
            target.VertexScratch.clear();
            target.ModelKey = 0;
        }
    }
    target.Player.Position = player.Position;
}

void FD3D9GameWorldScene::Impl::SetRemotePlayerAppearance(uint64 entityId, const FCharacterCreationAppearance& appearance)
{
    const auto iterator = RemotePlayers.find(entityId);
    if (iterator == RemotePlayers.end()) { return; }
    FRemotePlayerRenderState& target = iterator->second;
    if (target.Player.Appearance && SameAppearance(*target.Player.Appearance, appearance)) { return; }
    target.Player.Appearance = appearance;
    SafeRelease(target.VertexBuffer);
    target.SkinScratch.clear();
    target.VertexScratch.clear();
    target.ModelKey = 0;
}

void FD3D9GameWorldScene::Impl::RemoveRemotePlayer(uint64 entityId)
{
    const auto iterator = RemotePlayers.find(entityId);
    if (iterator == RemotePlayers.end()) { return; }
    SafeRelease(iterator->second.VertexBuffer);
    RemotePlayers.erase(iterator);
}

void FD3D9GameWorldScene::Impl::ClearRemotePlayers()
{
    for (auto& [_, remote] : RemotePlayers) { SafeRelease(remote.VertexBuffer); }
    RemotePlayers.clear();
    for (auto& [_, resource] : RemotePlayerModels)
    {
        for (auto& batch : resource.Batches) { SafeRelease(batch.Texture); }
        SafeRelease(resource.IndexBuffer);
    }
    RemotePlayerModels.clear();
}
