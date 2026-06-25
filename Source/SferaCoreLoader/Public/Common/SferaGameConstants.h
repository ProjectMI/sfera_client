#pragma once
#include "Core/Types.h"

namespace Sfera
{
constexpr int32 CharacterSlotCount = 3;
constexpr int32 DefaultKarma = 3;
constexpr int32 DefaultCharacterVital = 100;
constexpr int32 DefaultTitleId = 14;
constexpr int32 DefaultDegreeId = 112;
constexpr int32 DefaultTitleLevel = 1;
constexpr int32 DefaultDegreeLevel = 1;
constexpr int32 DefaultProgressNextXp = 50;
constexpr int32 CharacterModelBase = 0x30;
constexpr float InitialCharacterSceneAngle = 3.14159265358979323846f;
}

namespace SferaProtocol
{
constexpr uint16 GameFrameOpcode = 300;
constexpr uint16 ServerFrameOpcode = GameFrameOpcode;
constexpr uint16 LegacyConnectionLimitOpcode = 100;
constexpr uint16 LegacyHandshakeOpcode = 200;
constexpr uint16 LegacyAckOpcode = 400;
constexpr size_t ShortStatusFrameSize = 14;
constexpr size_t CharacterSelectStartFrameSize = 82;
constexpr size_t CharacterSlotFrameSize = 108;
constexpr uint8 ServerChannelByte = 0x08;
constexpr uint8 ServerFamilyByte = 0x40;
constexpr uint8 CharacterSelectMarker = 0x80;
constexpr uint8 CharacterSlotMarker = 0x60;
constexpr uint8 CannotConnectMarker = 0xA0;
constexpr uint8 CharacterSelectStartState = 0x10;
constexpr uint8 EmptySlotState = 0x79;
constexpr uint8 NameCheckPassedState = 0x00;
constexpr uint8 NameAlreadyExistsMarker = 0x00;
constexpr uint8 NameAlreadyExistsState = 0x01;
constexpr uint8 ClientActionByte = 0x04;
constexpr uint8 CreateCharacterAction = 0x05;
constexpr uint8 DeleteCharacterAction = 0x0d;
constexpr uint8 SlotWireStride = 4;
}
