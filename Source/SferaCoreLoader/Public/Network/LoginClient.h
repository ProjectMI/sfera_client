#pragma once
#include "Network/NetworkTypes.h"
#include "Network/SphereEmuProtocol.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using FCharacterSlotInfo = FCharacterSlot;
struct FLoginProbeResult;

struct FCharacterActionResult 
{
    bool Ok = false;
    bool Disconnected = false;
    int32 PacketCount = 0;
    int32 ByteCount = 0;
    std::vector<std::vector<uint8>> Frames;
    std::string Message;
};

class FServerSession 
{
public:
    ~FServerSession();
    FServerSession(const FServerSession&) = delete;
    FServerSession& operator=(const FServerSession&) = delete;
    FCharacterActionResult SelectCharacter(int32 slot, int32 timeoutMs = 2500);
    FCharacterActionResult CreateCharacter(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance, int32 timeoutMs = 2500);
    FCharacterActionResult DeleteCharacter(int32 slot, int32 timeoutMs = 2500);
    FCharacterActionResult SendIngameAck(int32 timeoutMs = 2500);
    FCharacterActionResult PollFrames(int32 maxFrames = 32);
    void Close();
    bool SendPosition(double x, double y, double z, double angle, std::string& error);
    bool Connected() const;
    uint16 LocalId() const;
private:
    struct FImpl;
    explicit FServerSession(std::unique_ptr<FImpl> impl);
    static std::shared_ptr<FServerSession> Create(std::unique_ptr<FImpl> impl);
    std::unique_ptr<FImpl> Impl;
    friend FLoginProbeResult ProbeLoginServer(const FEndpoint& endpoint, const std::wstring& login, const std::wstring& password, const FCharacterAppearanceRules& appearanceRules, int32 timeoutMs);
};

struct FLoginProbeResult 
{
    bool Connected = false;
    bool LegacyHandshake = false;
    bool CharacterSelectReady = false;
    uint16 LocalId = 0;
    int32 FirstOpcode = 0;
    int32 NextOpcode = 0;
    int32 FirstLength = 0;
    int32 NextLength = 0;
    int32 CharacterSelectPackets = 0;
    int32 CharacterSelectBytes = 0;
    std::shared_ptr<FServerSession> Session;
    std::array<FCharacterSlotInfo, 3> CharacterSlots{};
    std::string Message;
};

FLoginProbeResult ProbeLoginServer(const FEndpoint& endpoint, const std::wstring& login, const std::wstring& password, const FCharacterAppearanceRules& appearanceRules, int32 timeoutMs = 2500);