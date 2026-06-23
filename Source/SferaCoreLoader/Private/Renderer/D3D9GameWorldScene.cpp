#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

FD3D9GameWorldScene::FD3D9GameWorldScene()
    : ImplPtr(std::make_unique<Impl>())
{
}

FD3D9GameWorldScene::~FD3D9GameWorldScene() = default;

bool FD3D9GameWorldScene::Initialize(
    HWND hwnd,
    IDirect3DDevice9* device,
    const FResourceManager& terrainResources,
    const FWorldScene& world,
    const FGameWorldConfig& config,
    double spawnX,
    double spawnY,
    double spawnZ,
    double spawnAngle,
    std::wstring& error,
    FLogger* logger,
    const FSkinnedCharacterModel* playerModel)
{
    return ImplPtr->Initialize(
        hwnd,
        device,
        terrainResources,
        world,
        config,
        spawnX,
        spawnY,
        spawnZ,
        spawnAngle,
        error,
        logger,
        playerModel);
}

bool FD3D9GameWorldScene::SetOverlayBitmap(int width, int height, std::vector<uint8> bgraPixels, std::wstring& error)
{
    return ImplPtr->SetOverlayBitmap(width, height, std::move(bgraPixels), error);
}

bool FD3D9GameWorldScene::SetGrassQuality(int quality, std::wstring& error)
{
    return ImplPtr->SetGrassQuality(quality, error);
}

void FD3D9GameWorldScene::SetFog(float start, float end)
{
    ImplPtr->SetFog(start, end);
}

void FD3D9GameWorldScene::SetGameTime(float dayFraction)
{
    ImplPtr->SetGameTime(dayFraction);
}

float FD3D9GameWorldScene::CurrentGameTime() const
{
    return ImplPtr ? ImplPtr->GameTimeFraction : 0.0f;
}

float FD3D9GameWorldScene::CameraFacing() const
{
    return ImplPtr ? ImplPtr->CameraYaw : 0.0f;
}

bool FD3D9GameWorldScene::Update(float deltaSeconds, const FGameMovementInput& input, std::wstring& error)
{
    return ImplPtr->Update(deltaSeconds, input, error);
}

void FD3D9GameWorldScene::RotateView(float mouseDx, float mouseDy)
{
    ImplPtr->RotateView(mouseDx, mouseDy);
}

void FD3D9GameWorldScene::Jump()
{
    ImplPtr->Jump();
}

FGameWorldPosition FD3D9GameWorldScene::Position() const
{
    return ImplPtr->Position();
}

void FD3D9GameWorldScene::Resize()
{
    ImplPtr->Resize();
}

void FD3D9GameWorldScene::RenderInsideScene(const RECT& viewport)
{
    ImplPtr->RenderInsideScene(viewport);
}

FD3D9GameWorldRenderStats FD3D9GameWorldScene::RenderStats() const
{
    return ImplPtr ? ImplPtr->RenderStats() : FD3D9GameWorldRenderStats{};
}

bool FD3D9GameWorldScene::IsValid() const
{
    return ImplPtr && ImplPtr->Initialized;
}

void FD3D9GameWorldScene::Shutdown()
{
    if (ImplPtr)
    {
        ImplPtr->Release();
    }
}

FGameWorldConfig FD3D9GameWorldScene::DefaultConfig()
{
    FGameWorldConfig config;
    config.Ok = true;
    config.ModelDirs = {L"models", L"Models_hr", L"Models_ph", L"Models_rd"};
    config.StaticObjectDirs = {L"params"};
    config.TerrainMicrotexture = L"landscape/bs_.mtx";
    config.GrassmapDir = L"landscape/grassmap";
    config.SkyTexture = L"landscape/clouds.dds";
    config.CameraMode = L"first_person";
    config.GrassDetailModels = {L"grass_s00", L"grass_s01", L"grass_s02", L"grass_s03"};
    config.GrassSampleOffsets = {
        {6.24f, 3.73f},
        {2.21f, 1.17f},
        {2.21f, 5.60f},
        {6.24f, 7.15f},
    };
    config.SkyStates = {
        {0.000f, 0, 0, 0, 53, 57, 83, 60, 70, 100, 105, 105, 105},
        {0.167f, 0, 0, 0, 96, 102, 148, 60, 70, 100, 105, 105, 105},
        {0.200f, 40, 35, 35, 140, 140, 140, 0, 0, 0, 140, 105, 70},
        {0.266f, 90, 104, 122, 115, 105, 100, 120, 60, 23, 169, 104, 34},
        {0.335f, 50, 160, 250, 97, 118, 142, 243, 202, 166, 200, 200, 200},
        {0.667f, 50, 160, 250, 97, 118, 142, 243, 202, 166, 200, 200, 200},
        {0.733f, 90, 104, 122, 115, 105, 100, 120, 60, 23, 169, 104, 34},
        {0.810f, 40, 35, 35, 120, 120, 120, 0, 0, 0, 140, 110, 70},
        {0.840f, 0, 0, 0, 103, 103, 123, 0, 0, 0, 115, 115, 115},
        {0.864f, 0, 0, 0, 96, 102, 148, 60, 70, 100, 105, 105, 105},
    };

    auto setGrassPattern = [&](int index, std::initializer_list<const wchar_t*> values)
    {
        for (const wchar_t* value : values)
        {
            config.GrassPatterns[index].emplace_back(value);
        }
    };

    setGrassPattern(1, {L"grass002", L"grass002", L"grass002", L"grass014", L"grass014"});
    setGrassPattern(2, {L"grass018", L"grass018", L"grass018", L"grass006", L"grass003"});
    setGrassPattern(3, {L"grass003", L"grass003", L"grass003", L"grass009", L"grass004"});
    setGrassPattern(4, {L"grass010", L"grass010", L"grass002", L"grass002", L"grass005"});
    setGrassPattern(5, {L"grass009", L"grass009", L"grass009", L"grass004", L"grass000"});
    setGrassPattern(6, {L"grass016", L"grass016", L"grass016", L"grass004", L"grass011"});
    setGrassPattern(7, {L"grass014", L"grass014", L"grass007", L"grass007", L"grass007"});
    setGrassPattern(8, {L"grass013", L"grass013", L"grass013", L"grass005", L"grass004"});
    setGrassPattern(9, {L"grass007", L"grass007", L"grass003", L"grass003", L"grass013"});
    setGrassPattern(10, {L"grass002", L"grass002", L"grass009", L"grass009", L"grass003"});
    setGrassPattern(11, {L"grass012", L"grass012", L"grass005", L"grass005", L"grass013"});
    setGrassPattern(12, {L"grass012", L"grass012", L"grass012", L"grass004", L"grass009"});
    setGrassPattern(13, {L"grass007", L"grass007", L"grass007", L"grass007", L"grass015"});
    setGrassPattern(14, {L"grass017", L"grass017", L"grass017", L"grass005", L"grass005"});
    setGrassPattern(15, {L"grass001", L"grass001", L"grass001", L"grass001", L"grass007"});

    for (int index = 16; index <= 30; ++index)
    {
        config.GrassPatterns[index] = {L"grass000", L"grass000", L"grass000", L"grass000", L"grass000"};
    }

    config.GrassPatterns[19] = {L"grass101", L"grass101", L"grass101", L"grass101", L"grass101"};
    config.GrassPatterns[20] = {L"grass102", L"grass102", L"grass102", L"grass102", L"grass102"};
    config.GrassPatterns[21] = {L"grass100", L"grass100", L"grass100", L"grass101", L"grass101"};
    config.GrassPatterns[22] = {L"grass101", L"grass101", L"grass101", L"grass102", L"grass102"};
    config.GrassPatterns[23] = {L"grass100", L"grass100", L"grass100", L"grass102", L"grass102"};
    config.GrassPatterns[24] = {L"grass100", L"grass100", L"grass100", L"grass101", L"grass102"};
    config.GrassPatterns[25] = {L"grass100", L"grass100", L"grass101", L"grass102", L"grass102"};
    config.GrassPatterns[26] = {L"grass101", L"grass101", L"grass101", L"grass101", L"grass101"};
    config.GrassPatterns[27] = {L"grass102", L"grass102", L"grass102", L"grass102", L"grass102"};
    config.GrassPatterns[28] = {L"grass100", L"grass100", L"grass100", L"grass101", L"grass101"};
    config.GrassPatterns[29] = {L"grass101", L"grass101", L"grass101", L"grass102", L"grass102"};
    config.GrassPatterns[30] = {L"grass100", L"grass100", L"grass100", L"grass102", L"grass102"};

    config.GrassFlowerPatterns[1] = {L"flower1_21", L"flower1_41"};
    config.GrassFlowerPatterns[2] = {L"flower1_11", L"flower1_31"};
    config.GrassFlowerPatterns[3] = {L"flower1_21", L"flower1_61"};
    config.GrassFlowerPatterns[5] = {L"flower1_21", L"flower1_41", L"flower1_41"};
    config.GrassFlowerPatterns[6] = {L"flower1_11", L"flower1_21", L"flower1_31"};
    config.GrassFlowerPatterns[7] = {L"flower1_41", L"flower1_51"};
    config.GrassFlowerPatterns[10] = {L"flower1_11", L"flower1_31"};
    config.GrassFlowerPatterns[12] = {L"flower1_61"};
    config.GrassFlowerPatterns[13] = {L"flower1_11", L"flower1_21", L"flower1_51"};

    return config;
}
