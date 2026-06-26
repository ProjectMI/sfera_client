#pragma once
#include "Common/SferaGameConstants.h"
#include "Core/Types.h"

struct FCharacterSlot 
{ 
    bool Present = false;
    bool CanCreate = true;
    int32 Slot = 0;
    std::wstring Name; 
    bool Female = false; 
    int32 Face = 0; 
    int32 Hair = 0;
    int32 HairColor = 0;
    int32 Tattoo = 0;
    int32 MaxHp = 0;
    int32 MaxMp = 0; 
    int32 CurrentHp = 0; 
    int32 CurrentMp = 0;
    int32 MaxSatiety = 0;
    int32 CurrentSatiety = 0;
    int32 Strength = 0;
    int32 Dexterity = 0; 
    int32 Accuracy = 0;
    int32 Endurance = 0;
    int32 Fire = 0; 
    int32 Water = 0; 
    int32 Earth = 0; 
    int32 Air = 0; 
    int32 PhysicalAttack = 0; 
    int32 MagicalAttack = 0; 
    int32 PhysicalDefense = 0;
    int32 MagicalDefense = 0; 
    int32 TitleId = 0; 
    int32 DegreeId = 0;
    int32 TitleLevel = 1; 
    int32 DegreeLevel = 1; 
    int32 TitleXp = 0; 
    int32 DegreeXp = 0;
    int32 TitleNextXp = 50; 
    int32 DegreeNextXp = 50;
    int32 TitleStats = 0; 
    int32 DegreeStats = 0;
    int32 Karma = Sfera::DefaultKarma; 
};

struct FServerWorldPosition
{
    uint64 EntityId = 0;
    bool CharacterEntity = false;
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double Angle = 0.0;
};

struct FCharacterCreationAppearance
{
    bool Female = false; 
    int32 ModelBase = Sfera::CharacterModelBase; 
    int32 Face = 0; 
    int32 Hair = 0;
    int32 HairColor = 0;
    int32 Tattoo = 0; 
};

struct FCharacterAppearanceRules 
{ 
    int32 ModelBase = Sfera::CharacterModelBase;
    int32 MaleFaceCount = 13; 
    int32 FemaleFaceCount = 12;
    int32 MaleHairCount = 3;
    int32 FemaleHairCount = 5;
    int32 HairColorCount = 4; 
    int32 TattooCount = 4; 
};

class FSphereEmuProtocol 
{
public:
    static void WriteU16LE(FByteArray& data, size_t offset, uint16 value);
    static uint16 ReadU16LE(const FByteArray& data, size_t offset);
    static FByteArray ToCp1251(const std::wstring& text);
    static std::wstring FromCp1251(const FByteArray& bytes);
    static FByteArray BuildLegacyPacket(uint16 opcode, const FByteArray& payload, uint16& sequence, uint16 xorKey);
    static FByteArray BuildLoginPacket(uint16 localId, const std::wstring& login, const std::wstring& password);
    static FByteArray BuildCharacterSelectPacket(uint16 localId, int32 slot);
    static FByteArray BuildCreateCharacterPacket(uint16 localId, int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance);
    static FByteArray BuildDeleteCharacterPacket(uint16 localId, int32 slot);
    static FByteArray BuildIngameAckPacket(uint16 localId);
    static std::optional<FServerWorldPosition> TryParseServerWorldPosition(const FByteArray& frame);
    static std::optional<FServerWorldPosition> TryParseServerWorldPosition(const FByteArray& frame, uint16 preferredEntityId);
    static FByteArray EncodeClientPacket(const FByteArray& decoded);
    static std::string MakeCharacterName(const std::wstring& login);
    static std::array<uint8, 5> BuildCharacterCreationBytes(const FCharacterCreationAppearance& appearance);
    static bool LooksLikeCannotConnect(const FByteArray& frame);
    static bool LooksLikeCharacterSelectStart(const FByteArray& frame);
    static bool LooksLikeEmptyCharacterSlot(const FByteArray& frame);
    static bool LooksLikeCharacterSlot(const FByteArray& frame);
    static bool LooksLikeNameCheckPassed(const FByteArray& frame);
    static bool LooksLikeNameAlreadyExists(const FByteArray& frame);
    static FCharacterSlot ParseCharacterSlot(const FByteArray& frame, int32 slot, const FCharacterAppearanceRules& rules);
};
