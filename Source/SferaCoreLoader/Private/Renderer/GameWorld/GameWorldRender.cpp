#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

namespace
{
constexpr int kSkyRings = 8;
constexpr int kSkySegments = 32;
constexpr std::size_t kSkyVertexCount = (kSkyRings + 1) * (kSkySegments + 1);
constexpr std::size_t kSkyIndexCount = kSkyRings * kSkySegments * 6;

std::array<uint16, kSkyIndexCount> BuildSkyIndices()
{
    std::array<uint16, kSkyIndexCount> indices{};
    std::size_t cursor = 0;
    for (int ring = 0; ring < kSkyRings; ++ring)
    {
        for (int segment = 0; segment < kSkySegments; ++segment)
        {
            const auto a = static_cast<uint16>(ring * (kSkySegments + 1) + segment);
            const auto b = static_cast<uint16>(a + kSkySegments + 1);
            indices[cursor++] = a;
            indices[cursor++] = b;
            indices[cursor++] = static_cast<uint16>(a + 1);
            indices[cursor++] = static_cast<uint16>(a + 1);
            indices[cursor++] = b;
            indices[cursor++] = static_cast<uint16>(b + 1);
        }
    }
    return indices;
}
}

RECT FD3D9GameWorldScene::Impl::ClientRect() const
{
    RECT rc{};
    GetClientRect(Hwnd, &rc);
    rc.right = (std::max)(rc.right, rc.left + 1);
    rc.bottom = (std::max)(rc.bottom, rc.top + 1);
    return rc;
}

void FD3D9GameWorldScene::Impl::FillPresentParameters()
{
    const RECT rc = ClientRect();
    Present.BackBufferWidth = static_cast<UINT>(rc.right - rc.left);
    Present.BackBufferHeight = static_cast<UINT>(rc.bottom - rc.top);
    Present.BackBufferFormat = D3DFMT_UNKNOWN;
    Present.BackBufferCount = 1;
    Present.MultiSampleType = D3DMULTISAMPLE_NONE;
    Present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Present.hDeviceWindow = Hwnd;
    Present.Windowed = TRUE;
    Present.EnableAutoDepthStencil = TRUE;
    Present.AutoDepthStencilFormat = D3DFMT_D24S8;
    Present.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
}

void FD3D9GameWorldScene::Impl::CreateReflectionTarget()
{
    ReflectionTextureReady = false;
    ReflectionWarmupFrames = 3;
    ReflectionUpdateCountdown = 0;
    SafeRelease(ReflectionDepth);
    SafeRelease(ReflectionSurface);
    SafeRelease(ReflectionTexture);
    if (!Device)
    {
        return;
    }
    if (FAILED(Device->CreateTexture(
        kReflectionSize,
        kReflectionSize,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &ReflectionTexture,
        nullptr)))
    {
        ReflectionTexture = nullptr;
        return;
    }
    ReflectionTexture->GetSurfaceLevel(0, &ReflectionSurface);
    if (FAILED(Device->CreateDepthStencilSurface(
        kReflectionSize,
        kReflectionSize,
        Present.AutoDepthStencilFormat,
        D3DMULTISAMPLE_NONE,
        0,
        TRUE,
        &ReflectionDepth,
        nullptr)))
    {
        ReflectionDepth = nullptr;
    }
}

void FD3D9GameWorldScene::Impl::LoadWorldShaders()
{
    const auto VSPath = ResolveOptionalPath("shaders/vertex/00_00_00_00.vsc");
    const auto VSCode = VSPath.empty() ? FByteArray{} : ReadBinaryFile(VSPath);
    const auto PSPath = ResolveOptionalPath("shaders/pixel/00_00_00_00_00.psc");
    const auto PSCode = PSPath.empty() ? FByteArray{} : ReadBinaryFile(PSPath);
    if (VSCode.empty() || PSCode.empty())
    {
        return;
    }
    const auto VSWords = MakeShaderWords(VSCode);
    const auto PSWords = MakeShaderWords(PSCode);
    if (FAILED(Device->CreateVertexShader(VSWords.data(), &BaseVS)) ||
    FAILED(Device->CreatePixelShader(PSWords.data(), &BasePS)))
    {
        SafeRelease(BaseVS);
        SafeRelease(BasePS);
        return;
    }
    BaseVSConsts = ParseShaderConstants(VSCode);

    const std::array<D3DVERTEXELEMENT9, 6> Elements
    {{
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 28, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 36, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()
    }};
    if (FAILED(Device->CreateVertexDeclaration(Elements.data(), &WorldDecl)))
    {
        SafeRelease(BaseVS);
        SafeRelease(BasePS);
        SafeRelease(WorldDecl);
        return;
    }
    WorldShadersReady = true;

    try
    {
        const auto GrassVSPath = ResolveOptionalPath("shaders/custom/grass_wind.vsc");
        const auto GrassPSPath = ResolveOptionalPath("shaders/custom/grass_wind.psc");
        const auto GrassVSCode = GrassVSPath.empty() ? FByteArray{} : ReadGameWorldFileBytes(GrassVSPath);
        const auto GrassPSCode = GrassPSPath.empty() ? FByteArray{} : ReadGameWorldFileBytes(GrassPSPath);
        if (!GrassVSCode.empty() && !GrassPSCode.empty())
        {
            const auto GrassVSWords = MakeShaderWords(GrassVSCode);
            const auto GrassPSWords = MakeShaderWords(GrassPSCode);
            if (FAILED(Device->CreateVertexShader(GrassVSWords.data(), &GrassVS)) || FAILED(Device->CreatePixelShader(GrassPSWords.data(), &GrassPS)))
            {
                SafeRelease(GrassVS);
                SafeRelease(GrassPS);
            }
        }
    }
    catch (...)
    {
        SafeRelease(GrassVS);
        SafeRelease(GrassPS);
    }
}

void FD3D9GameWorldScene::Impl::SetVsConst(const char* name, const float* data, int Vec4Count)
{
    const auto it = BaseVSConsts.find(name);
    if (it != BaseVSConsts.end())
    {
        Device->SetVertexShaderConstantF(static_cast<UINT>(it->second), data, static_cast<UINT>(Vec4Count));
    }
}

void FD3D9GameWorldScene::Impl::SetBaseLightConstants()
{
    const std::array<float, 8> Colors
    {
        Environment.SunRed / 255.0f,
        Environment.SunGreen / 255.0f,
        Environment.SunBlue / 255.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };
    SetVsConst("gDirLightColor", Colors.data(), 2);

    const std::array<float, 4> Ambient
    {
        Environment.AmbientRed / 255.0f,
        Environment.AmbientGreen / 255.0f,
        Environment.AmbientBlue / 255.0f,
        1.0f
    };
    SetVsConst("gAmbientColor", Ambient.data(), 1);
}


bool FD3D9GameWorldScene::Impl::IsBoundsVisibleToCamera(const FBox3& Bounds, float ExtraMargin) const
{
    const FVector3 min{Bounds.Min.X - ExtraMargin, Bounds.Min.Y - ExtraMargin, Bounds.Min.Z - ExtraMargin};
    const FVector3 max{Bounds.Max.X + ExtraMargin, Bounds.Max.Y + ExtraMargin, Bounds.Max.Z + ExtraMargin};
    struct ClipPoint
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 1.0f;
    };
    std::array<ClipPoint, 8> corners{};
    const auto& m = ViewProjectionMatrix;
    for (int i = 0; i < 8; ++i)
    {
        const float x = (i & 1) ? max.X : min.X;
        const float y = (i & 2) ? max.Y : min.Y;
        const float z = (i & 4) ? max.Z : min.Z;
        corners[static_cast<std::size_t>(i)] = ClipPoint{
            x * m._11 + y * m._21 + z * m._31 + m._41,
            x * m._12 + y * m._22 + z * m._32 + m._42,
            x * m._13 + y * m._23 + z * m._33 + m._43,
            x * m._14 + y * m._24 + z * m._34 + m._44};
    }
    auto outside = [&](auto pred)
    {
        for (const auto& corner : corners)
        {
            if (!pred(corner))
            {
                return false;
            }
        }
        return true;
    };
    if (outside([](const ClipPoint& p) { return p.X < -p.W; })) { return false; }
    if (outside([](const ClipPoint& p) { return p.X > p.W; })) { return false; }
    if (outside([](const ClipPoint& p) { return p.Y < -p.W; })) { return false; }
    if (outside([](const ClipPoint& p) { return p.Y > p.W; })) { return false; }
    if (outside([](const ClipPoint& p) { return p.Z < 0.0f; })) { return false; }
    if (outside([](const ClipPoint& p) { return p.Z > p.W; })) { return false; }
    return true;
}

void FD3D9GameWorldScene::Impl::SetBaseWorld(const D3DMATRIX& world)
{
    const auto& vp = ViewProjectionMatrix;
    const std::array<float, 16> WvpConstants
    {
        world._11 * vp._11 + world._12 * vp._21 + world._13 * vp._31 + world._14 * vp._41,
        world._21 * vp._11 + world._22 * vp._21 + world._23 * vp._31 + world._24 * vp._41,
        world._31 * vp._11 + world._32 * vp._21 + world._33 * vp._31 + world._34 * vp._41,
        world._41 * vp._11 + world._42 * vp._21 + world._43 * vp._31 + world._44 * vp._41,
        world._11 * vp._12 + world._12 * vp._22 + world._13 * vp._32 + world._14 * vp._42,
        world._21 * vp._12 + world._22 * vp._22 + world._23 * vp._32 + world._24 * vp._42,
        world._31 * vp._12 + world._32 * vp._22 + world._33 * vp._32 + world._34 * vp._42,
        world._41 * vp._12 + world._42 * vp._22 + world._43 * vp._32 + world._44 * vp._42,
        world._11 * vp._13 + world._12 * vp._23 + world._13 * vp._33 + world._14 * vp._43,
        world._21 * vp._13 + world._22 * vp._23 + world._23 * vp._33 + world._24 * vp._43,
        world._31 * vp._13 + world._32 * vp._23 + world._33 * vp._33 + world._34 * vp._43,
        world._41 * vp._13 + world._42 * vp._23 + world._43 * vp._33 + world._44 * vp._43,
        world._11 * vp._14 + world._12 * vp._24 + world._13 * vp._34 + world._14 * vp._44,
        world._21 * vp._14 + world._22 * vp._24 + world._23 * vp._34 + world._24 * vp._44,
        world._31 * vp._14 + world._32 * vp._24 + world._33 * vp._34 + world._34 * vp._44,
        world._41 * vp._14 + world._42 * vp._24 + world._43 * vp._34 + world._44 * vp._44
    };
    SetVsConst("gWorldViewProjection", WvpConstants.data(), 4);

    constexpr float LightX = 0.40452f;
    constexpr float LightY = 0.86683f;
    constexpr float LightZ = -0.52009f;
    std::array<float, 8> Local
    {
        LightX * world._11 + LightY * world._12 + LightZ * world._13,
        LightX * world._21 + LightY * world._22 + LightZ * world._23,
        LightX * world._31 + LightY * world._32 + LightZ * world._33,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f
    };
    const float Length = std::sqrt(Local[0] * Local[0] + Local[1] * Local[1] + Local[2] * Local[2]);
    if (Length > 0.0001f)
    {
        const float InvLength = 1.0f / Length;
        Local[0] *= InvLength;
        Local[1] *= InvLength;
        Local[2] *= InvLength;
    }
    SetVsConst("gDirLightToLightDirL", Local.data(), 2);
}

void FD3D9GameWorldScene::Impl::ComputeWindCircles(float Out[12]) const
{
    static constexpr float RadiusX[6] = {34.0f, 22.0f, 41.0f, 17.0f, 29.0f, 12.0f};
    static constexpr float RadiusZ[6] = {28.0f, 39.0f, 15.0f, 33.0f, 20.0f, 44.0f};
    static constexpr float SpeedA[6] = {0.13f, 0.19f, 0.11f, 0.23f, 0.16f, 0.27f};
    static constexpr float SpeedB[6] = {0.17f, 0.09f, 0.21f, 0.14f, 0.25f, 0.12f};
    static constexpr float PhaseA[6] = {0.0f, 1.1f, 2.3f, 3.7f, 4.9f, 5.5f};
    static constexpr float PhaseB[6] = {1.7f, 0.4f, 5.1f, 2.8f, 3.3f, 0.9f};
    for (int i = 0; i < 6; ++i)
    {
        Out[i * 2] = SpawnX + RadiusX[i] * std::sin(ElapsedSeconds * SpeedA[i] + PhaseA[i]);
        Out[i * 2 + 1] = SpawnZ + RadiusZ[i] * std::sin(ElapsedSeconds * SpeedB[i] + PhaseB[i]);
    }
}

void FD3D9GameWorldScene::Impl::BeginBaseShader()
{
    Device->SetVertexDeclaration(WorldDecl);
    Device->SetVertexShader(BaseVS);
    Device->SetPixelShader(BasePS);
}

void FD3D9GameWorldScene::Impl::EndBaseShader()
{
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
}

void FD3D9GameWorldScene::Impl::ConfigureRenderState()
{
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    Device->SetRenderState(D3DRS_LIGHTING, TRUE);
    Device->SetRenderState(
    D3DRS_AMBIENT,
    D3DCOLOR_XRGB(Environment.AmbientRed, Environment.AmbientGreen, Environment.AmbientBlue));
    Device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    Device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    Device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    Device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    Device->SetRenderState(D3DRS_FOGENABLE, TRUE);
    Device->SetRenderState(
    D3DRS_FOGCOLOR,
    D3DCOLOR_XRGB(Environment.ClearRed, Environment.ClearGreen, Environment.ClearBlue));
    Device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
    Device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
    const DWORD FogStart = FloatRenderStateValue(Config.FogStart);
    const DWORD FogEnd = FloatRenderStateValue(Config.FogEnd);
    Device->SetRenderState(D3DRS_FOGSTART, FogStart);
    Device->SetRenderState(D3DRS_FOGEND, FogEnd);

    D3DMATERIAL9 material{};
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
    material.Ambient = material.Diffuse;
    Device->SetMaterial(&material);

    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = static_cast<float>(Environment.SunRed) / 255.0f;
    light.Diffuse.g = static_cast<float>(Environment.SunGreen) / 255.0f;
    light.Diffuse.b = static_cast<float>(Environment.SunBlue) / 255.0f;
    light.Ambient.r = light.Ambient.g = light.Ambient.b = 0.0f;
    light.Direction.x = -0.35f;
    light.Direction.y = -0.75f;
    light.Direction.z = 0.45f;
    Device->SetLight(0, &light);
    Device->LightEnable(0, TRUE);
}

void FD3D9GameWorldScene::Impl::UpdateViewProjection()
{
    const RECT rc = ClientRect();
    const float aspect = static_cast<float>(rc.right - rc.left) / static_cast<float>(rc.bottom - rc.top);
    FVector3 eye{SpawnX, SpawnY - Config.CameraEyeHeight, SpawnZ};
    if (PlayerEyeValid)
    {
        const float c = std::cos(CameraYaw);
        const float s = std::sin(CameraYaw);
        eye.X = SpawnX + PlayerEyeLocalX * c + PlayerEyeLocalZ * s;
        eye.Y = SpawnY + PlayerEyeLocalY;
        eye.Z = SpawnZ - PlayerEyeLocalX * s + PlayerEyeLocalZ * c;
        if (PlayerWalking)
        {
            eye.Y += (PlayerLiveCrownY - PlayerLockedCrownY) * kWalkBobScale;
        }
    }
    const float HorizontalDistance = std::cos(CameraPitch) * Config.CameraLookDistance;
    const FVector3 target{
        eye.X + std::sin(CameraYaw) * HorizontalDistance,
        eye.Y - std::sin(CameraPitch) * Config.CameraLookDistance,
        eye.Z + std::cos(CameraYaw) * HorizontalDistance,
    };
    CameraEye = eye;
    CameraTarget = target;
    const auto view = LookAtRhMatrix(eye, target, FVector3{0.0f, -1.0f, 0.0f});
    const auto projection = PerspectiveFovRhMatrix(Config.CameraFov * kPi / 180.0f, aspect, Config.NearClip, Config.FarClip);
    ViewMatrix = view;
    ProjectionMatrix = projection;
    ViewProjectionMatrix = MultiplyMatrix(view, projection);
    Device->SetTransform(D3DTS_VIEW, &view);
    Device->SetTransform(D3DTS_PROJECTION, &projection);
}

void FD3D9GameWorldScene::Impl::DrawSky()
{
    std::array<WorldVertex, kSkyVertexCount> vertices{};
    static const auto Indices = BuildSkyIndices();
    std::size_t vertexCursor = 0;
    const DWORD color = D3DCOLOR_XRGB(Environment.CloudRed, Environment.CloudGreen, Environment.CloudBlue);
    const float scroll = ElapsedSeconds * Config.SkyScrollSpeed;
    for (int ring = 0; ring <= kSkyRings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(kSkyRings);
        const float theta = v * kPi * 0.5f;
        const float radial = std::sin(theta) * Config.SkyRadius;
        const float height = std::cos(theta) * Config.SkyRadius * Config.SkyHeightScale;
        for (int segment = 0; segment <= kSkySegments; ++segment)
        {
            const float longitude = static_cast<float>(segment) / static_cast<float>(kSkySegments);
            const float angle = longitude * 2.0f * kPi;
            const float nx = Config.SkyRadius > 0.0f ? std::cos(angle) * radial / Config.SkyRadius : 0.0f;
            const float nz = Config.SkyRadius > 0.0f ? std::sin(angle) * radial / Config.SkyRadius : 0.0f;
            vertices[vertexCursor++] = WorldVertex{SpawnX + std::cos(angle) * radial, SpawnY - height, SpawnZ + std::sin(angle) * radial, 0.0f, -1.0f, 0.0f, color, 0.5f + nx * 0.5f + scroll, 0.5f + nz * 0.5f, 0.0f, 0.0f};
        }
    }

    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Device->SetFVF(kWorldVertexFvf);
    Device->SetTexture(0, SkyTexture);
    const auto identity = IdentityMatrix();
    Device->SetTransform(D3DTS_WORLD, &identity);
    const UINT triangleCount = static_cast<UINT>(Indices.size() / 3);
    Device->DrawIndexedPrimitiveUP(
    D3DPT_TRIANGLELIST,
    0,
    static_cast<UINT>(vertices.size()),
    triangleCount,
    Indices.data(),
    D3DFMT_INDEX16,
    vertices.data(),
    sizeof(WorldVertex));
    RecordWorldDraw(triangleCount, EGameWorldDrawBucket::Sky);
    ConfigureRenderState();
}

void FD3D9GameWorldScene::Impl::DrawOverlay()
{
    if (!OverlayTexture || OverlayWidth <= 0 || OverlayHeight <= 0)
    {
        return;
    }
    const float w = static_cast<float>(OverlayWidth);
    const float h = static_cast<float>(OverlayHeight);
    const std::array<OverlayVertex, 4> Quad
    {{
        {-0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f},
        {w - 0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f},
        {w - 0.5f, h - 0.5f, 0.0f, 1.0f, 0xffffffff, 1.0f, 1.0f},
        {-0.5f, h - 0.5f, 0.0f, 1.0f, 0xffffffff, 0.0f, 1.0f}
    }};
    Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Device->SetFVF(kOverlayVertexFvf);
    Device->SetTexture(0, OverlayTexture);
    Device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, Quad.data(), sizeof(OverlayVertex));
    RecordWorldDraw(2, EGameWorldDrawBucket::Overlay);
    ConfigureRenderState();
}


void FD3D9GameWorldScene::Impl::ResetRenderStats()
{
    LastRenderStats = FD3D9GameWorldRenderStats{};
    LastRenderStats.FrameId = ++RenderStatsFrameCounter;
    LastRenderStats.TerrainResources = TerrainResources.size();
    LastRenderStats.TerrainInstances = TerrainInstances.size();
    LastRenderStats.StaticResources = StaticResources.size();
    LastRenderStats.StaticInstances = StaticInstances.size();
    LastRenderStats.GrassInstances = GrassInstances.size();
    LastRenderStats.GrassMaps = GrassMaps.size();
    LastRenderStats.GrassCells = GrassCells.size();
}

void FD3D9GameWorldScene::Impl::RecordWorldDraw(uint32 triangles, EGameWorldDrawBucket bucket)
{
    ++LastRenderStats.DrawCalls;
    LastRenderStats.Triangles += triangles;
    if (RenderingReflection)
    {
        ++LastRenderStats.ReflectionDrawCalls;
        LastRenderStats.ReflectionTriangles += triangles;
    }
    switch (bucket)
    {
        case EGameWorldDrawBucket::Sky: ++LastRenderStats.SkyDrawCalls; break;
        case EGameWorldDrawBucket::Terrain: ++LastRenderStats.TerrainDrawCalls; break;
        case EGameWorldDrawBucket::StaticObjects: ++LastRenderStats.StaticDrawCalls; break;
        case EGameWorldDrawBucket::Grass: ++LastRenderStats.GrassDrawCalls; break;
        case EGameWorldDrawBucket::Player: ++LastRenderStats.PlayerDrawCalls; break;
        case EGameWorldDrawBucket::Water: ++LastRenderStats.WaterDrawCalls; break;
        case EGameWorldDrawBucket::Overlay: ++LastRenderStats.OverlayDrawCalls; break;
    }
}

FD3D9GameWorldRenderStats FD3D9GameWorldScene::Impl::RenderStats() const
{
    return LastRenderStats;
}

void FD3D9GameWorldScene::Impl::Resize()
{
    if (!Device)
    {
        return;
    }
    FillPresentParameters();
    CreateReflectionTarget();
    ConfigureRenderState();
}

void FD3D9GameWorldScene::Impl::RenderInsideScene(const RECT&)
{
    if (!Initialized || !Device)
    {
        return;
    }
    {
        UpdateViewProjection();
    }
    ResetRenderStats();
    if (ReflectionWarmupFrames > 0)
    {
        --ReflectionWarmupFrames;
    }
    else if (ReflectionUpdateCountdown <= 0)
    {
        RenderReflection();
        ReflectionUpdateCountdown = ReflectionTextureReady ? 7 : 0;
    }
    else
    {
        --ReflectionUpdateCountdown;
    }
    {
        Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(Environment.ClearRed, Environment.ClearGreen, Environment.ClearBlue), 1.0f, 0);
    }
    DrawSky();
    DrawTerrain();
    DrawGrass();
    DrawStaticObjects();
    DrawPlayer();
    DrawWater();
    DrawOverlay();
}
