#include "Renderer/GameWorld/D3D9GameWorldSceneImpl.h"

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

bool FD3D9GameWorldScene::Impl::CreateDevice(std::wstring& error)
{
    D3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!D3D)
    {
        error = L"Direct3DCreate9 failed";
        return false;
    }
    FillPresentParameters();
    const std::array<DWORD, 2> Flags{D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING};
    const std::array<D3DFORMAT, 2> Depths{D3DFMT_D24S8, D3DFMT_D16};
    HRESULT LastHr = E_FAIL;
    for (const auto Depth : Depths)
    {
        Present.AutoDepthStencilFormat = Depth;
        for (const auto Flag : Flags)
        {
            LastHr = D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Hwnd, Flag, &Present, &Device);
            if (SUCCEEDED(LastHr))
            {
                return true;
            }
        }
    }
    error = HResultText("CreateDevice", LastHr);
    return false;
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

    const auto DebugPath = ResolveOptionalPath("shaders/pixel/debug_tc.psc");
    const auto dbg = DebugPath.empty() ? FByteArray{} : ReadBinaryFile(DebugPath);
    if (!dbg.empty())
    {
        const auto DebugWords = MakeShaderWords(dbg);
        Device->CreatePixelShader(DebugWords.data(), &DebugPS);
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
        EnvironmentSunRed / 255.0f,
        EnvironmentSunGreen / 255.0f,
        EnvironmentSunBlue / 255.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };
    SetVsConst("gDirLightColor", Colors.data(), 2);

    const std::array<float, 4> Ambient
    {
        EnvironmentAmbientRed / 255.0f,
        EnvironmentAmbientGreen / 255.0f,
        EnvironmentAmbientBlue / 255.0f,
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

bool FD3D9GameWorldScene::Impl::IsPointVisibleToCamera(float x, float y, float z, float Radius) const
{
    const FVector3 toPoint{x - CameraEye.X, y - CameraEye.Y, z - CameraEye.Z};
    FVector3 forward{CameraTarget.X - CameraEye.X, CameraTarget.Y - CameraEye.Y, CameraTarget.Z - CameraEye.Z};
    const float forwardLength = std::sqrt(Dot(forward, forward));
    if (forwardLength <= 0.0001f)
    {
        return true;
    }
    const float invForwardLength = 1.0f / forwardLength;
    forward = Scale(forward, invForwardLength);
    const float depth = Dot(toPoint, forward);
    if (depth < -Radius || depth > Config.FarClip + Radius)
    {
        return false;
    }
    if (depth <= Radius)
    {
        return true;
    }
    FVector3 up{0.0f, -1.0f, 0.0f};
    FVector3 right = Cross(up, forward);
    float rightLength = std::sqrt(Dot(right, right));
    if (rightLength <= 0.0001f)
    {
        up = FVector3{0.0f, 0.0f, 1.0f};
        right = Cross(up, forward);
        rightLength = std::sqrt(Dot(right, right));
        if (rightLength <= 0.0001f)
        {
            return true;
        }
    }
    right = Scale(right, 1.0f / rightLength);
    const FVector3 realUp = Cross(forward, right);
    if (std::abs(Dot(toPoint, right)) > depth * FrustumTanHalfX + Radius)
    {
        return false;
    }
    if (std::abs(Dot(toPoint, realUp)) > depth * FrustumTanHalfY + Radius)
    {
        return false;
    }
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
    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    Device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    Device->SetRenderState(D3DRS_LIGHTING, TRUE);
    Device->SetRenderState(
    D3DRS_AMBIENT,
    D3DCOLOR_XRGB(EnvironmentAmbientRed, EnvironmentAmbientGreen, EnvironmentAmbientBlue));
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
    D3DCOLOR_XRGB(EnvironmentClearRed, EnvironmentClearGreen, EnvironmentClearBlue));
    // Use PIXEL (table) fog, not vertex fog: vertex fog requires the vertex
    // geometry (terrain/objects/player) was getting an undefined fog factor
    // and rendering solid fog-blue. Table fog is computed per-pixel from depth
    // and works for both the fixed-function and shader passes.
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
    light.Diffuse.r = static_cast<float>(EnvironmentSunRed) / 255.0f;
    light.Diffuse.g = static_cast<float>(EnvironmentSunGreen) / 255.0f;
    light.Diffuse.b = static_cast<float>(EnvironmentSunBlue) / 255.0f;
    // Ambient comes solely from the global D3DRS_AMBIENT (the data-driven
    // environment ambient). The old fixed 0.35 here double-counted ambient
    // and washed the scene flat.
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
    ViewAspect = aspect;
    FrustumTanHalfY = std::tan(Config.CameraFov * kPi / 360.0f) * 1.15f;
    FrustumTanHalfX = FrustumTanHalfY * ViewAspect * 1.15f;
    // Sphere's renderer uses positive Y downward; the server/Godot side
    // stores the same Position with the Y sign reversed.
    // The first-person camera rides at the player's head bone (so the body
    // is visible below and the near clip culls the player's own head),
    // falling back to a fixed eye height before the model is skinned.
    FVector3 eye{SpawnX, SpawnY - Config.CameraEyeHeight, SpawnZ};
    if (PlayerEyeValid)
    {
        // Eye offset rotates with the body (CameraYaw), keeping the lens at
        // the face front regardless of facing.
        const float c = std::cos(CameraYaw);
        const float s = std::sin(CameraYaw);
        eye.X = SpawnX + PlayerEyeLocalX * c + PlayerEyeLocalZ * s;
        eye.Y = SpawnY + PlayerEyeLocalY;
        eye.Z = SpawnZ - PlayerEyeLocalX * s + PlayerEyeLocalZ * c;
        // While walking, let the eye follow the head-top's vertical motion
        // through the stride for a small up/down bob (idle stays locked).
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
    constexpr int rings = 8;
    constexpr int segments = 32;
    auto& vertices = SkyVertices;
    auto& Indices = SkyIndices;
    vertices.clear();
    vertices.reserve((rings + 1) * (segments + 1));
    if (Indices.empty())
    {
        Indices.reserve(rings * segments * 6);
    }
    const DWORD color = D3DCOLOR_XRGB(EnvironmentCloudRed, EnvironmentCloudGreen, EnvironmentCloudBlue);
    const float scroll = ElapsedSeconds * Config.SkyScrollSpeed;
    for (int ring = 0; ring <= rings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float theta = v * kPi * 0.5f;
        const float radial = std::sin(theta) * Config.SkyRadius;
        const float height = std::cos(theta) * Config.SkyRadius * Config.SkyHeightScale;
        for (int segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float Angle = u * 2.0f * kPi;
            vertices.push_back(WorldVertex{
                SpawnX + std::cos(Angle) * radial,
                SpawnY - height,
                SpawnZ + std::sin(Angle) * radial,
                0.0f,
                -1.0f,
                0.0f,
                color,
                u + scroll,
                v,
                0.0f,
                0.0f,
            });
        }
    }
    if (Indices.empty())
    {
        for (int ring = 0; ring < rings; ++ring)
        {
            for (int segment = 0; segment < segments; ++segment)
            {
                const auto a = static_cast<uint16>(ring * (segments + 1) + segment);
                const auto b = static_cast<uint16>(a + segments + 1);
                Indices.push_back(a);
                Indices.push_back(b);
                Indices.push_back(static_cast<uint16>(a + 1));
                Indices.push_back(static_cast<uint16>(a + 1));
                Indices.push_back(b);
                Indices.push_back(static_cast<uint16>(b + 1));
            }
        }
    }

    // clouds.dds is a DARK cloud texture (clouds = bright, clear sky = near
    // black) meant to be ADDED over the sky-coloured background, not drawn
    // opaque. The frame is already cleared to the sky colour (environment
    // clear), so blend the tinted cloud texture additively: bright cloud
    // texels brighten the blue sky into clouds, dark texels leave it blue.
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
    UpdateViewProjection();
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
    Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(EnvironmentClearRed, EnvironmentClearGreen, EnvironmentClearBlue), 1.0f, 0);
    DrawSky();
    DrawTerrain();
    DrawGrass();
    DrawStaticObjects();
    DrawPlayer();
    DrawWater();
    DrawOverlay();
}
