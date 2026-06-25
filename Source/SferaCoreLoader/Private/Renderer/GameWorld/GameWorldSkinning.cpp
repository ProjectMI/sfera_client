#include "Renderer/GameWorld/SkinnedCharacterModel.h"
#include "Common/StringUtils.h"

static float SkinDot(FVector3 A, FVector3 B)
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

static FVector3 SkinNormalize(FVector3 Value)
{
    const float Length = std::sqrt(SkinDot(Value, Value));

    if (Length <= 0.000001f)
    {
        return {0.0f, 1.0f, 0.0f};
    }

    return {Value.X / Length, Value.Y / Length, Value.Z / Length};
}

static FSceneMatrix4 SkinIdentity()
{
    FSceneMatrix4 Matrix{};
    Matrix[0] = 1.0f;
    Matrix[5] = 1.0f;
    Matrix[10] = 1.0f;
    Matrix[15] = 1.0f;
    return Matrix;
}

static FSceneMatrix4 SkinMatrixFromTransform(const FSklTransform& Transform)
{
    float X = Transform.QX;
    float Y = Transform.QY;
    float Z = Transform.QZ;
    float W = Transform.QW;
    const float Length = std::sqrt(X * X + Y * Y + Z * Z + W * W);

    if (Length <= 0.00001f)
    {
        X = 0.0f;
        Y = 0.0f;
        Z = 0.0f;
        W = 1.0f;
    }
    else
    {
        X /= Length;
        Y /= Length;
        Z /= Length;
        W /= Length;
    }

    FSceneMatrix4 Matrix = SkinIdentity();
    Matrix[0] = 1.0f - 2.0f * Y * Y - 2.0f * Z * Z;
    Matrix[1] = 2.0f * X * Y + 2.0f * Z * W;
    Matrix[2] = 2.0f * X * Z - 2.0f * Y * W;
    Matrix[4] = 2.0f * X * Y - 2.0f * Z * W;
    Matrix[5] = 1.0f - 2.0f * X * X - 2.0f * Z * Z;
    Matrix[6] = 2.0f * Y * Z + 2.0f * X * W;
    Matrix[8] = 2.0f * X * Z + 2.0f * Y * W;
    Matrix[9] = 2.0f * Y * Z - 2.0f * X * W;
    Matrix[10] = 1.0f - 2.0f * X * X - 2.0f * Y * Y;
    Matrix[12] = Transform.TX;
    Matrix[13] = Transform.TY;
    Matrix[14] = Transform.TZ;
    return Matrix;
}

static FSklTransform SkinBlendTransform(const FSklTransform& A, const FSklTransform& B, float Alpha)
{
    const float T = std::clamp(Alpha, 0.0f, 1.0f);
    FSklTransform Out;
    Out.TX = A.TX + (B.TX - A.TX) * T;
    Out.TY = A.TY + (B.TY - A.TY) * T;
    Out.TZ = A.TZ + (B.TZ - A.TZ) * T;
    const float DotQuat = A.QW * B.QW + A.QX * B.QX + A.QY * B.QY + A.QZ * B.QZ;
    const float Sign = DotQuat < 0.0f ? -1.0f : 1.0f;
    Out.QW = A.QW + (B.QW * Sign - A.QW) * T;
    Out.QX = A.QX + (B.QX * Sign - A.QX) * T;
    Out.QY = A.QY + (B.QY * Sign - A.QY) * T;
    Out.QZ = A.QZ + (B.QZ * Sign - A.QZ) * T;
    const float Length = std::sqrt(Out.QW * Out.QW + Out.QX * Out.QX + Out.QY * Out.QY + Out.QZ * Out.QZ);
    if (Length <= 0.000001f)
    {
        Out.QW = 1.0f;
        Out.QX = 0.0f;
        Out.QY = 0.0f;
        Out.QZ = 0.0f;
    }
    else
    {
        const float Inv = 1.0f / Length;
        Out.QW *= Inv;
        Out.QX *= Inv;
        Out.QY *= Inv;
        Out.QZ *= Inv;
    }
    return Out;
}

static FSceneMatrix4 SkinMultiply(FSceneMatrix4 A, FSceneMatrix4 B)
{
    FSceneMatrix4 Out{};

    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Column = 0; Column < 4; ++Column)
        {
            Out[Row * 4 + Column] =
                A[Row * 4 + 0] * B[0 * 4 + Column]
                + A[Row * 4 + 1] * B[1 * 4 + Column]
                + A[Row * 4 + 2] * B[2 * 4 + Column]
                + A[Row * 4 + 3] * B[3 * 4 + Column];
        }
    }

    return Out;
}

static FVector3 SkinTransformPoint(const FSceneMatrix4& Matrix, FVector3 Value)
{
    return
    {
        Value.X * Matrix[0] + Value.Y * Matrix[4] + Value.Z * Matrix[8] + Matrix[12],
        Value.X * Matrix[1] + Value.Y * Matrix[5] + Value.Z * Matrix[9] + Matrix[13],
        Value.X * Matrix[2] + Value.Y * Matrix[6] + Value.Z * Matrix[10] + Matrix[14]
    };
}

static FVector3 SkinTransformVector(const FSceneMatrix4& Matrix, FVector3 Value)
{
    return
    {
        Value.X * Matrix[0] + Value.Y * Matrix[4] + Value.Z * Matrix[8],
        Value.X * Matrix[1] + Value.Y * Matrix[5] + Value.Z * Matrix[9],
        Value.X * Matrix[2] + Value.Y * Matrix[6] + Value.Z * Matrix[10]
    };
}

static std::vector<FSceneMatrix4> BuildSkinMatrices(const FSkinnedCharacterModel& Model, std::size_t Frame)
{
    const auto& Skeleton = Model.Skeleton;

    if (Skeleton.BoneCount <= 0 || Skeleton.FrameCount <= 0 || Frame >= static_cast<std::size_t>(Skeleton.FrameCount))
    {
        throw std::runtime_error("SKL frame out of range");
    }

    const std::size_t BoneCount = static_cast<std::size_t>(Skeleton.BoneCount);

    if (Skeleton.Parents.size() < BoneCount || Skeleton.Transforms.size() < (Frame + 1) * BoneCount)
    {
        throw std::runtime_error("SKL data is truncated");
    }

    std::vector<FSceneMatrix4> Matrices(BoneCount);
    std::vector<uint8> States(BoneCount, 0);

    std::function<FSceneMatrix4(std::size_t)> Resolve = [&](std::size_t Bone) -> FSceneMatrix4
    {
        if (States[Bone] == 2)
        {
            return Matrices[Bone];
        }

        if (States[Bone] == 1)
        {
            throw std::runtime_error("SKL parent hierarchy cycle");
        }

        States[Bone] = 1;
        FSceneMatrix4 Matrix = SkinMatrixFromTransform(Skeleton.Transforms[Frame * BoneCount + Bone]);
        const int32 Parent = Skeleton.Parents[Bone];

        if (Parent >= 0)
        {
            Matrix = SkinMultiply(Matrix, Resolve(static_cast<std::size_t>(Parent)));
        }

        Matrices[Bone] = Matrix;
        States[Bone] = 2;
        return Matrix;
    };

    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        Resolve(Index);
    }

    return Matrices;
}

bool FSkinnedCharacterModel::IsValid() const
{
    return Skeleton.BoneCount > 0
        && Skeleton.FrameCount > 0
        && !Skeleton.Parents.empty()
        && !Skeleton.Transforms.empty()
        && !Skeleton.AnimationFrameCounts.empty()
        && !Sources.empty()
        && !Indices.empty()
        && !Batches.empty();
}

std::size_t FSkinnedCharacterModel::ActionCount() const
{
    return Skeleton.AnimationFrameCounts.size();
}

std::size_t FSkinnedCharacterModel::ActionFrameStart(std::size_t Action) const
{
    if (Action >= Skeleton.AnimationFrameCounts.size())
    {
        return 0;
    }

    std::size_t Offset = 0;

    for (std::size_t Index = 0; Index < Action; ++Index)
    {
        Offset += static_cast<std::size_t>(std::max(0, Skeleton.AnimationFrameCounts[Index]));
    }

    return Offset;
}

std::size_t FSkinnedCharacterModel::ActionFrameCount(std::size_t Action) const
{
    if (Action >= Skeleton.AnimationFrameCounts.size())
    {
        return 0;
    }

    return static_cast<std::size_t>(std::max(0, Skeleton.AnimationFrameCounts[Action]));
}

int FSkinnedCharacterModel::BoneIndex(const char* Name) const
{
    const std::string Wanted = Common::ToLower(Name ? std::string(Name) : std::string());

    for (std::size_t Index = 0; Index < Skeleton.BoneNames.size(); ++Index)
    {
        if (Common::ToLower(Skeleton.BoneNames[Index]) == Wanted)
        {
            return static_cast<int>(Index);
        }
    }

    return -1;
}

static std::vector<FSceneMatrix4> BuildSkinMatricesInterpolated(const FSkinnedCharacterModel& Model, std::size_t FrameA, std::size_t FrameB, float Alpha)
{
    const auto& Skeleton = Model.Skeleton;

    if (Skeleton.BoneCount <= 0 || Skeleton.FrameCount <= 0 || FrameA >= static_cast<std::size_t>(Skeleton.FrameCount) || FrameB >= static_cast<std::size_t>(Skeleton.FrameCount))
    {
        throw std::runtime_error("SKL frame out of range");
    }

    const std::size_t BoneCount = static_cast<std::size_t>(Skeleton.BoneCount);

    if (Skeleton.Parents.size() < BoneCount || Skeleton.Transforms.size() < (std::max)(FrameA, FrameB) * BoneCount + BoneCount)
    {
        throw std::runtime_error("SKL data is truncated");
    }

    std::vector<FSceneMatrix4> Matrices(BoneCount);
    std::vector<uint8> States(BoneCount, 0);

    std::function<FSceneMatrix4(std::size_t)> Resolve = [&](std::size_t Bone) -> FSceneMatrix4
    {
        if (States[Bone] == 2)
        {
            return Matrices[Bone];
        }

        if (States[Bone] == 1)
        {
            throw std::runtime_error("SKL parent hierarchy cycle");
        }

        States[Bone] = 1;
        const auto& TransformA = Skeleton.Transforms[FrameA * BoneCount + Bone];
        const auto& TransformB = Skeleton.Transforms[FrameB * BoneCount + Bone];
        FSceneMatrix4 Matrix = SkinMatrixFromTransform(SkinBlendTransform(TransformA, TransformB, Alpha));
        const int32 Parent = Skeleton.Parents[Bone];

        if (Parent >= 0)
        {
            Matrix = SkinMultiply(Matrix, Resolve(static_cast<std::size_t>(Parent)));
        }

        Matrices[Bone] = Matrix;
        States[Bone] = 2;
        return Matrix;
    };

    for (std::size_t Index = 0; Index < BoneCount; ++Index)
    {
        Resolve(Index);
    }

    return Matrices;
}

static void SkinWithMatrices(const FSkinnedCharacterModel& Model, std::vector<FSceneMatrix4>& Matrices, std::vector<float>& Out)
{
    Out.clear();

    if (Model.RootBone >= Matrices.size())
    {
        throw std::runtime_error("character root bone out of range");
    }

    const FVector3 RootDelta
    {
        Matrices[Model.RootBone][12] - Model.RootBindX,
        Matrices[Model.RootBone][13] - Model.RootBindY,
        Matrices[Model.RootBone][14] - Model.RootBindZ
    };

    for (auto& Matrix : Matrices)
    {
        Matrix[12] -= RootDelta.X;
        Matrix[13] -= RootDelta.Y;
        Matrix[14] -= RootDelta.Z;
    }

    Out.resize(Model.Sources.size() * 8);

    for (std::size_t Index = 0; Index < Model.Sources.size(); ++Index)
    {
        const auto& Source = Model.Sources[Index];

        if (Source.Bone0 >= Matrices.size() || Source.Bone1 >= Matrices.size())
        {
            throw std::runtime_error("skinned source bone out of range");
        }

        const auto Matrix0 = Matrices[Source.Bone0];
        const auto Matrix1 = Matrices[Source.Bone1];
        const float Weight0 = std::clamp(Source.Blend, 0.0f, 1.0f);
        const float Weight1 = 1.0f - Weight0;
        const FVector3 P0 = SkinTransformPoint(Matrix0, {Source.X, Source.Y, Source.Z});
        const FVector3 P1 = SkinTransformPoint(Matrix1, {Source.X, Source.Y, Source.Z});
        const FVector3 N0 = SkinTransformVector(Matrix0, {Source.NX, Source.NY, Source.NZ});
        const FVector3 N1 = SkinTransformVector(Matrix1, {Source.NX, Source.NY, Source.NZ});
        const FVector3 SkinnedPosition
        {
            P0.X * Weight0 + P1.X * Weight1,
            P0.Y * Weight0 + P1.Y * Weight1,
            P0.Z * Weight0 + P1.Z * Weight1
        };
        const FVector3 NormalSource = SkinNormalize({N0.X * Weight0 + N1.X * Weight1, N0.Y * Weight0 + N1.Y * Weight1, N0.Z * Weight0 + N1.Z * Weight1});
        const FVector3 Normal = SkinNormalize({NormalSource.X, -NormalSource.Y, NormalSource.Z});
        float* Dest = Out.data() + Index * 8;
        Dest[0] = (SkinnedPosition.X - Model.CenterX) * Model.Scale;
        Dest[1] = ((-SkinnedPosition.Y) - Model.MinY) * Model.Scale;
        Dest[2] = (SkinnedPosition.Z - Model.CenterZ) * Model.Scale;
        Dest[3] = Normal.X;
        Dest[4] = Normal.Y;
        Dest[5] = Normal.Z;
        Dest[6] = Source.U;
        Dest[7] = Source.V;
    }
}

void SkinFrame(const FSkinnedCharacterModel& Model, std::size_t Frame, std::vector<float>& Out)
{
    auto Matrices = BuildSkinMatrices(Model, Frame);
    SkinWithMatrices(Model, Matrices, Out);
}

void SkinFrameInterpolated(const FSkinnedCharacterModel& Model, std::size_t FrameA, std::size_t FrameB, float Alpha, std::vector<float>& Out)
{
    auto Matrices = BuildSkinMatricesInterpolated(Model, FrameA, FrameB, Alpha);
    SkinWithMatrices(Model, Matrices, Out);
}
