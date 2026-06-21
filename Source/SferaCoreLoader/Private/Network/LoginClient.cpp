#include "Network/LoginClient.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    class FWsaSession
    {
    public:
        FWsaSession()
        {
            OkValue = WSAStartup(MAKEWORD(2, 2), &Data) == 0;
        }
        FWsaSession(const FWsaSession&) = delete;
        FWsaSession& operator=(const FWsaSession&) = delete;
        ~FWsaSession()
        {
            if (OkValue)
            {
                WSACleanup();
            }
        }
        bool Ok() const { return OkValue; }
    private:
        WSADATA Data{};
        bool OkValue = false;
    };

    class FSocketHandle
    {
    public:
        FSocketHandle() = default;
        explicit FSocketHandle(SOCKET socket) : Socket(socket) {}
        FSocketHandle(const FSocketHandle&) = delete;
        FSocketHandle& operator=(const FSocketHandle&) = delete;
        FSocketHandle(FSocketHandle&& other) noexcept
        {
            Socket = other.Socket;
            other.Socket = INVALID_SOCKET;
        }
        FSocketHandle& operator=(FSocketHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                Socket = other.Socket;
                other.Socket = INVALID_SOCKET;
            }

            return *this;
        }
        ~FSocketHandle()
        {
            Reset();
        }
        explicit operator bool() const { return Socket != INVALID_SOCKET; }
        SOCKET Get() const { return Socket; }
        void Reset()
        {
            if (Socket != INVALID_SOCKET)
            {
                closesocket(Socket);
                Socket = INVALID_SOCKET;
            }
        }
    private:
        SOCKET Socket = INVALID_SOCKET;
    };

    uint16 ReadU16Le(const std::vector<uint8>& data, size_t offset)
    {
        if (offset + 1 >= data.size()) { return 0; }

        return static_cast<uint16>(data[offset] | (data[offset + 1] << 8));
    }

    void WriteU16Le(std::vector<uint8>& data, size_t offset, uint16 value)
    {
        data[offset] = static_cast<uint8>(value & 0xff);
        data[offset + 1] = static_cast<uint8>((value >> 8) & 0xff);
    }

    std::string WsaErrorText(const char* prefix)
    {
        std::ostringstream out;
        out << prefix << " (WSA " << WSAGetLastError() << ")";
        return out.str();
    }

    bool WaitSocket(SOCKET socket, bool write, int32 timeoutMs)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(socket, &fds);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int rc = select(0, write ? nullptr : &fds, write ? &fds : nullptr, nullptr, &tv);
        return rc > 0 && FD_ISSET(socket, &fds);
    }

    FSocketHandle ConnectSocket(const std::string& host, int32 port, int32 timeoutMs, std::string& error)
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* addresses = nullptr;
        const std::string service = std::to_string(port);
        const int gai = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);

        if (gai != 0) { error = "resolve failed: " + std::string(gai_strerrorA(gai)); return {}; }

        FSocketHandle connected;

        for (addrinfo* addr = addresses; addr != nullptr; addr = addr->ai_next)
        {
            FSocketHandle socketHandle(socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol));

            if (!socketHandle) { continue; }

            u_long nonblocking = 1;
            ioctlsocket(socketHandle.Get(), FIONBIO, &nonblocking);
            const int rc = connect(socketHandle.Get(), addr->ai_addr, static_cast<int>(addr->ai_addrlen));

            if (rc == 0 || WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS)
            {
                if (rc == 0 || WaitSocket(socketHandle.Get(), true, timeoutMs))
                {
                    int socketError = 0;
                    int socketErrorSize = sizeof(socketError);
                    getsockopt(socketHandle.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorSize);

                    if (socketError == 0) { nonblocking = 0; ioctlsocket(socketHandle.Get(), FIONBIO, &nonblocking); connected = std::move(socketHandle); break; }

                    WSASetLastError(socketError);
                }
            }
        }

        freeaddrinfo(addresses);

        if (!connected)
        {
            error = WsaErrorText("connect failed");
        }

        return connected;
    }

    bool RecvExact(SOCKET socket, uint8* out, int32 size, int32 timeoutMs)
    {
        int32 total = 0;

        while (total < size)
        {
            if (!WaitSocket(socket, false, timeoutMs)) { return false; }

            const int rc = recv(socket, reinterpret_cast<char*>(out + total), size - total, 0);

            if (rc <= 0) { return false; }

            total += rc;
        }

        return true;
    }

    bool RecvFrame(SOCKET socket, std::vector<uint8>& frame, int32 timeoutMs)
    {
        uint8 header[2]{};

        if (!RecvExact(socket, header, 2, timeoutMs)) { return false; }

        const int32 length = header[0] | (header[1] << 8);

        if (length < 4 || length > 60000) { return false; }

        frame.assign(static_cast<size_t>(length), 0);
        frame[0] = header[0];
        frame[1] = header[1];
        return RecvExact(socket, frame.data() + 2, length - 2, timeoutMs);
    }

    bool SendAll(SOCKET socket, const std::vector<uint8>& data)
    {
        int32 sent = 0;

        while (sent < static_cast<int32>(data.size()))
        {
            const int rc = send(socket, reinterpret_cast<const char*>(data.data() + sent), static_cast<int>(data.size()) - sent, 0);

            if (rc <= 0) { return false; }

            sent += rc;
        }

        return true;
    }

    std::vector<uint8> ToCp1251(const std::wstring& text)
    {
        if (text.empty()) { return {}; }

        const int required = WideCharToMultiByte(1251, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, "?", nullptr);
        std::vector<uint8> out(static_cast<size_t>(required > 0 ? required : 0));

        if (required > 0)
        {
            WideCharToMultiByte(1251, 0, text.c_str(), static_cast<int>(text.size()), reinterpret_cast<char*>(out.data()), required, "?", nullptr);
        }

        return out;
    }

    std::wstring FromCp1251(const std::vector<uint8>& bytes)
    {
        if (bytes.empty()) { return {}; }

        const int required = MultiByteToWideChar(1251, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(required > 0 ? required : 0), L'\0');

        if (required > 0)
        {
            MultiByteToWideChar(1251, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), out.data(), required);
        }

        return out;
    }

    void WriteByteLsb(std::vector<uint8>& data, int32 bitPosition, uint8 value)
    {
        for (int32 bit = 0; bit < 8; ++bit)
        {
            if ((value & (1U << bit)) == 0) { continue; }

            const int32 absoluteBit = bitPosition + bit;
            data[static_cast<size_t>(absoluteBit / 8)] = static_cast<uint8>(data[static_cast<size_t>(absoluteBit / 8)] | (1U << (absoluteBit % 8)));
        }
    }

    std::vector<uint8> BuildLegacyPacket(uint16 opcode, const std::vector<uint8>& payload, uint16& sequence, uint16 xorKey)
    {
        const uint16 length = static_cast<uint16>(payload.size() + 8);
        std::vector<uint8> packet(length, 0);
        sequence = static_cast<uint16>(sequence + static_cast<uint16>((std::rand() & 3) + 1));
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 4, sequence);
        WriteU16Le(packet, 6, opcode);
        std::copy(payload.begin(), payload.end(), packet.begin() + 8);
        int16 sum = 0;

        for (size_t i = 4; i < packet.size(); ++i)
        {
            sum = static_cast<int16>(sum + static_cast<int8>(packet[i]));
        }

        WriteU16Le(packet, 2, static_cast<uint16>(xorKey ^ static_cast<uint16>(sum)));
        return packet;
    }

    std::vector<uint8> BuildSphereEmuLoginPacket(uint16 localId, const std::wstring& login, const std::wstring& password)
    {
        std::vector<uint8> strings = ToCp1251(login);
        strings.push_back(1);
        const auto passwordBytes = ToCp1251(password);
        strings.insert(strings.end(), passwordBytes.begin(), passwordBytes.end());
        strings.push_back(0);
        constexpr int32 stringBitOffset = 18 * 8 + 2;
        const int32 packetLength = (stringBitOffset + static_cast<int32>(strings.size()) * 8 + 7) / 8;
        std::vector<uint8> packet(static_cast<size_t>(packetLength), 0);
        WriteU16Le(packet, 0, static_cast<uint16>(packetLength));
        WriteU16Le(packet, 2, 300);
        const uint8 major = static_cast<uint8>((localId >> 8) & 0xff);
        const uint8 minor = static_cast<uint8>(localId & 0xff);
        packet[7] = major;
        packet[8] = minor;
        packet[11] = major;
        packet[12] = minor;
        int32 bitPosition = stringBitOffset;

        for (uint8 byte : strings)
        {
            WriteByteLsb(packet, bitPosition, byte);
            bitPosition += 8;
        }

        return packet;
    }

    std::vector<uint8> EncodeClientPacketForSphereEmu(const std::vector<uint8>& decoded)
    {
        constexpr uint8 encodingMask[] =
        {
            0x4B, 0x0D, 0xEF, 0x60, 0xC9, 0x9A, 0x70, 0x0E, 0x03
        };
        constexpr size_t start = 9;

        if (decoded.size() <= start) { return decoded; }

        std::vector<uint8> encoded = decoded;
        uint8 mask3 = 0;

        for (size_t i = 0; i + start < decoded.size(); ++i)
        {
            const uint8 current = decoded[i + start];
            encoded[i + start] = static_cast<uint8>(current ^ encodingMask[i % std::size(encodingMask)] ^ mask3);
            mask3 = static_cast<uint8>(current * i + 2 * mask3);
        }

        return encoded;
    }

    std::array<uint8, 5> BuildCharacterCreationBytes(const FCharacterCreationAppearance& appearance)
    {
        const int32 modelBase = std::clamp(appearance.ModelBase, 0, 255);
        auto modelValue = [modelBase](int32 localIndex)
        {
            return modelBase + std::clamp(localIndex, 0, 255 - modelBase);
        };
        int32 face = modelValue(appearance.Face);
        int32 hair = modelValue(appearance.Hair);
        int32 hairColor = modelValue(appearance.HairColor);
        int32 tattoo = modelValue(appearance.Tattoo);

        if (appearance.Female)
        {
            face = 256 - face;
            hair = 255 - hair;
            hairColor = 255 - hairColor;
            tattoo = 255 - tattoo;
        }

        std::array<uint8, 5> bytes{};
        bytes[0] = static_cast<uint8>((face & 0x03) << 6);
        bytes[1] = static_cast<uint8>(((face >> 2) & 0x3f) | ((hair & 0x03) << 6));
        bytes[2] = static_cast<uint8>(((hair >> 2) & 0x3f) | ((hairColor & 0x03) << 6));
        bytes[3] = static_cast<uint8>(((hairColor >> 2) & 0x3f) | ((tattoo & 0x03) << 6));
        bytes[4] = static_cast<uint8>((tattoo >> 2) & 0x3f);
        return bytes;
    }

    bool IsRussianNameSymbol(wchar_t ch) { return (ch >= static_cast<wchar_t>(0x0410) && ch <= static_cast<wchar_t>(0x042f)) || (ch >= static_cast<wchar_t>(0x0430) && ch <= static_cast<wchar_t>(0x044f)) || ch == static_cast<wchar_t>(0x0401) || ch == static_cast<wchar_t>(0x0451); }

    uint8 EncodeCharacterNameSymbol(wchar_t ch)
    {
        if (ch >= static_cast<wchar_t>(0x0410) && ch <= static_cast<wchar_t>(0x042f))
        {
            return static_cast<uint8>(129U + static_cast<unsigned>(ch - static_cast<wchar_t>(0x0410)) * 2U);
        }

        if (ch >= static_cast<wchar_t>(0x0430) && ch <= static_cast<wchar_t>(0x044f))
        {
            return static_cast<uint8>(193U + static_cast<unsigned>(ch - static_cast<wchar_t>(0x0430)) * 2U);
        }

        if (ch == static_cast<wchar_t>(0x0401))
        {
            return static_cast<uint8>(129U + static_cast<unsigned>(static_cast<wchar_t>(0x0415) - static_cast<wchar_t>(0x0410)) * 2U);
        }

        if (ch == static_cast<wchar_t>(0x0451))
        {
            return static_cast<uint8>(193U + static_cast<unsigned>(static_cast<wchar_t>(0x0435) - static_cast<wchar_t>(0x0430)) * 2U);
        }

        if (ch >= 0x20 && ch <= 0x7f)
        {
            return static_cast<uint8>(static_cast<unsigned>(ch) * 2U);
        }

        return static_cast<uint8>(L'?' * 2U);
    }

    std::vector<uint8> EncodeCharacterNameCheck(const std::wstring& name)
    {
        std::vector<uint8> bytes(name.size() + 1, 0);
        const bool serverFirstLetterHack = name.size() > 1 && IsRussianNameSymbol(name[0]) && IsRussianNameSymbol(name[1]);

        for (size_t i = 1; i <= name.size(); ++i)
        {
            unsigned code = static_cast<unsigned>(EncodeCharacterNameSymbol(name[i - 1]));

            if (i == 1 && serverFirstLetterHack && (code & 1U) != 0)
            {
                --code;
            }

            bytes[i - 1] = static_cast<uint8>((bytes[i - 1] & 0x1fU) | ((code & 0x07U) << 5));
            bytes[i] = static_cast<uint8>((bytes[i] & 0xe0U) | ((code >> 3) & 0x1fU));
        }

        return bytes;
    }

    std::vector<uint8> BuildCharacterSelectPacket(uint16 localId, int32 slot)
    {
        constexpr uint16 length = 0x15;
        std::vector<uint8> packet(length, 0);
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 2, 300);
        packet[6] = 0x04;
        packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
        packet[8] = static_cast<uint8>(localId & 0xff);
        packet[9] = 0x08;
        packet[10] = 0x40;
        packet[17] = static_cast<uint8>((std::clamp(slot, 0, 2) + 1) * 4);
        return packet;
    }

    std::vector<uint8> BuildCreateCharacterPacket(uint16 localId, int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance)
    {
        const auto nameCheck = EncodeCharacterNameCheck(name);
        const uint16 length = static_cast<uint16>(25 + nameCheck.size());
        std::vector<uint8> packet(length, 0);
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 2, 300);
        packet[6] = 0x04;
        packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
        packet[8] = static_cast<uint8>(localId & 0xff);
        packet[9] = 0x08;
        packet[10] = 0x40;
        packet[13] = 0x08;
        packet[14] = 0x40;
        packet[15] = 0x80;
        packet[16] = 0x05;
        packet[17] = static_cast<uint8>((std::clamp(slot, 0, 2) + 1) * 4);
        std::copy(nameCheck.begin(), nameCheck.end(), packet.begin() + 20);
        const auto charData = BuildCharacterCreationBytes(appearance);
        std::copy(charData.begin(), charData.end(), packet.end() - charData.size());
        return packet;
    }

    std::vector<uint8> BuildDeleteCharacterPacket(uint16 localId, int32 slot)
    {
        constexpr uint16 length = 0x2a;
        std::vector<uint8> packet(length, 0);
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 2, 300);
        packet[6] = 0x04;
        packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
        packet[8] = static_cast<uint8>(localId & 0xff);
        packet[9] = 0x08;
        packet[10] = 0x40;
        packet[13] = 0x08;
        packet[14] = 0x40;
        packet[15] = 0x80;
        packet[16] = 0x0d;
        packet[17] = static_cast<uint8>((std::clamp(slot, 0, 2) + 1) * 4);
        return packet;
    }

    std::vector<uint8> BuildIngameAckPacket(uint16 localId)
    {
        constexpr uint16 length = 0x13;
        std::vector<uint8> packet(length, 0);
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 2, 300);
        packet[6] = 0x04;
        packet[7] = static_cast<uint8>((localId >> 8) & 0xff);
        packet[8] = static_cast<uint8>(localId & 0xff);
        packet[9] = 0x08;
        packet[10] = 0x40;
        return packet;
    }

    void WriteClientCoordinate(std::vector<uint8>& packet, size_t offset, double value)
    {
        constexpr double fractionBase = 8388608.0;
        uint32 fraction = 0;
        int32 scale = 126;
        bool negative = false;

        if (std::abs(value) > 0.0000001)
        {
            negative = value < 0.0;
            const double absolute = std::abs(value);
            const int32 exponent = static_cast<int32>(std::floor(std::log2(absolute)));
            scale = std::clamp(exponent + 127, 0, 255);
            const double normalized = absolute / std::ldexp(1.0, exponent);
            fraction = static_cast<uint32>((normalized - 1.0) * fractionBase) & 0x7fffffU;
        }

        packet[offset] = static_cast<uint8>((packet[offset] & 0x3fU) | ((fraction & 0x3U) << 6));
        packet[offset + 1] = static_cast<uint8>((fraction >> 2) & 0xffU);
        packet[offset + 2] = static_cast<uint8>((fraction >> 10) & 0xffU);
        packet[offset + 3] = static_cast<uint8>(((fraction >> 18) & 0x1fU) | ((scale & 0x7) << 5));
        packet[offset + 4] = static_cast<uint8>((packet[offset + 4] & 0xc0U) | ((scale >> 3) & 0x1fU) | (negative ? 0x20U : 0U));
    }

    std::vector<uint8> BuildPositionPacket(uint16 localId, uint8 sequence, double x, double y, double z, double angle)
    {
        constexpr uint16 length = 0x26;
        std::vector<uint8> packet(length, 0);
        WriteU16Le(packet, 0, length);
        WriteU16Le(packet, 2, 300);
        packet[6] = 0x04;
        packet[9] = 0x08;
        packet[10] = 0x40;
        packet[11] = static_cast<uint8>((localId >> 8) & 0xff);
        packet[12] = static_cast<uint8>(localId & 0xff);
        packet[17] = sequence;
        WriteClientCoordinate(packet, 21, x);
        WriteClientCoordinate(packet, 25, y);
        WriteClientCoordinate(packet, 29, z);
        WriteClientCoordinate(packet, 33, angle);
        return packet;
    }

    bool LooksLikeCannotConnect(const std::vector<uint8>& frame) { return frame.size() == 14 && ReadU16Le(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0xA0; }
    bool LooksLikeCharacterSelectStart(const std::vector<uint8>& frame) { return frame.size() == 82 && ReadU16Le(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x80 && frame[12] == 0x10; }
    bool LooksLikeCharacterSlot(const std::vector<uint8>& frame) { return frame.size() == 108 && ReadU16Le(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x60; }
    bool LooksLikeNameCheckPassed(const std::vector<uint8>& frame) { return frame.size() == 14 && ReadU16Le(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x80 && frame[12] == 0x00; }
    bool LooksLikeNameAlreadyExists(const std::vector<uint8>& frame) { return frame.size() == 14 && ReadU16Le(frame, 2) == 300 && frame[9] == 0x08 && frame[10] == 0x40 && frame[11] == 0x00 && frame[12] == 0x01; }

    uint32 ReadBitsLE(const std::vector<uint8>& data, int32 bitOffset, int32 bitCount)
    {
        uint32 value = 0;

        for (int32 bit = 0; bit < bitCount; ++bit)
        {
            const int32 absolute = bitOffset + bit;
            const size_t byteIndex = static_cast<size_t>(absolute / 8);

            if (byteIndex >= data.size())
            {
                break;
            }

            if ((data[byteIndex] & (1U << (absolute % 8))) != 0)
            {
                value |= 1U << bit;
            }
        }

        return value;
    }

    int32 ReadPacked14(const std::vector<uint8>& data, size_t offset)
    {
        if (offset + 1 >= data.size()) { return 0; }

        return static_cast<int32>((data[offset] >> 2) | (data[offset + 1] << 6));
    }

    int32 AppearanceLocalIndex(int32 storedValue, int32 count, int32 modelBase)
    {
        if (count <= 0) { return -1; }

        if (storedValue >= modelBase && storedValue < modelBase + count) { return storedValue - modelBase; }

        return -1;
    }

    std::wstring DecodeSlotName(const std::vector<uint8>& frame)
    {
        std::vector<uint8> bytes;
        bytes.reserve(19);

        for (int32 i = 0; i < 19; ++i)
        {
            const uint8 decoded = static_cast<uint8>(ReadBitsLE(frame, 538 + i * 8, 8));

            if (decoded == 0) { break; }

            bytes.push_back(decoded);
        }

        return FromCp1251(bytes);
    }

    FCharacterSlotInfo ParseCharacterSlot(const std::vector<uint8>& frame, int32 slot, const FCharacterAppearanceRules& rules)
    {
        FCharacterSlotInfo out;
        out.Slot = slot;

        if (!LooksLikeCharacterSlot(frame)) { out.CanCreate = false; return out; }

        out.Name = DecodeSlotName(frame);
        out.Present = !out.Name.empty();
        out.CanCreate = !out.Present && frame.size() == 108 && frame[12] == 0x79;
        out.Female = ReadBitsLE(frame, 530, 8) != 0;
        out.MaxHp = static_cast<int32>(ReadBitsLE(frame, 106, 16));
        out.MaxMp = static_cast<int32>(ReadBitsLE(frame, 122, 16));
        out.Strength = static_cast<int32>(ReadBitsLE(frame, 138, 16));
        out.Dexterity = static_cast<int32>(ReadBitsLE(frame, 154, 16));
        out.Accuracy = static_cast<int32>(ReadBitsLE(frame, 170, 16));
        out.Endurance = static_cast<int32>(ReadBitsLE(frame, 186, 16));
        out.Earth = static_cast<int32>(ReadBitsLE(frame, 202, 16));
        out.Air = static_cast<int32>(ReadBitsLE(frame, 218, 16));
        out.Water = static_cast<int32>(ReadBitsLE(frame, 234, 16));
        out.Fire = static_cast<int32>(ReadBitsLE(frame, 250, 16));
        out.PhysicalDefense = static_cast<int32>(ReadBitsLE(frame, 266, 16));
        out.MagicalDefense = static_cast<int32>(ReadBitsLE(frame, 282, 16));
        out.Karma = static_cast<int32>(ReadBitsLE(frame, 298, 8));
        out.MaxSatiety = static_cast<int32>(ReadBitsLE(frame, 306, 16));
        out.TitleId = static_cast<int32>(ReadBitsLE(frame, 322, 16));
        out.DegreeId = static_cast<int32>(ReadBitsLE(frame, 338, 16));
        out.TitleXp = static_cast<int32>(ReadBitsLE(frame, 354, 32));
        out.DegreeXp = static_cast<int32>(ReadBitsLE(frame, 386, 32));
        out.CurrentSatiety = static_cast<int32>(ReadBitsLE(frame, 418, 16));
        out.CurrentHp = static_cast<int32>(ReadBitsLE(frame, 434, 16));
        out.CurrentMp = static_cast<int32>(ReadBitsLE(frame, 450, 16));
        out.TitleStats = static_cast<int32>(ReadBitsLE(frame, 466, 16));
        out.DegreeStats = static_cast<int32>(ReadBitsLE(frame, 482, 16));
        out.PhysicalAttack = 0;
        out.MagicalAttack = 0;
        out.TitleLevel = std::max(1, (out.TitleXp / std::max(1, out.TitleNextXp)) + 1);
        out.DegreeLevel = std::max(1, (out.DegreeXp / std::max(1, out.DegreeNextXp)) + 1);
        out.Face = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 690, 8)), out.Female ? rules.FemaleFaceCount : rules.MaleFaceCount, rules.ModelBase);
        out.Hair = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 698, 8)), out.Female ? rules.FemaleHairCount : rules.MaleHairCount, rules.ModelBase);
        out.HairColor = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 706, 8)), rules.HairColorCount, rules.ModelBase);
        out.Tattoo = AppearanceLocalIndex(static_cast<int32>(ReadBitsLE(frame, 714, 8)), rules.TattooCount, rules.ModelBase);
        return out;
    }

    std::string Cp1251String(const std::wstring& text)
    {
        const auto bytes = ToCp1251(text);
        return std::string(bytes.begin(), bytes.end());
    }

    void ReadActionFrames(SOCKET socket, FCharacterActionResult& result, int32 maxFrames, int32 timeoutMs)
    {
        std::vector<uint8> frame;

        while (result.PacketCount < maxFrames && RecvFrame(socket, frame, timeoutMs))
        {
            ++result.PacketCount;
            result.ByteCount += static_cast<int32>(frame.size());
            result.Frames.push_back(frame);
        }
    }

    void ReadAvailableFrames(SOCKET socket, FCharacterActionResult& result, int32 maxFrames)
    {
        while (result.PacketCount < maxFrames)
        {
            u_long available = 0;

            if (ioctlsocket(socket, FIONREAD, &available) != 0 || available < 2) { break; }

            uint8 header[2]{};

            if (recv(socket, reinterpret_cast<char*>(header), 2, MSG_PEEK) != 2) { break; }

            const int32 length = header[0] | (header[1] << 8);

            if (length < 4 || length > 60000 || available < static_cast<u_long>(length)) { break; }

            std::vector<uint8> frame;

            if (!RecvFrame(socket, frame, 0)) { break; }

            ++result.PacketCount;
            result.ByteCount += static_cast<int32>(frame.size());
            result.Frames.push_back(std::move(frame));
        }
    }
}

struct FServerSession::FImpl
{
    FWsaSession Wsa;
    FSocketHandle Socket;
    uint16 LocalId = 0;
    uint8 PositionSequence = 0;
};

FServerSession::FServerSession(std::unique_ptr<FImpl> impl) : Impl(std::move(impl)) {}
FServerSession::~FServerSession() = default;
bool FServerSession::Connected() const { return Impl && static_cast<bool>(Impl->Socket); }
uint16 FServerSession::LocalId() const { return Impl ? Impl->LocalId : 0; }
void FServerSession::Close()
{
    if (Impl)
    {
        Impl->Socket.Reset();
    }
}

FCharacterActionResult FServerSession::SelectCharacter(int32 slot, int32 timeoutMs)
{
    FCharacterActionResult result;

    if (!Connected()) { result.Message = "character session is closed"; return result; }

    const auto packet = EncodeClientPacketForSphereEmu(BuildCharacterSelectPacket(Impl->LocalId, slot));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character select send failed"); return result; }

    ReadActionFrames(Impl->Socket.Get(), result, 1, timeoutMs);
    result.Ok = result.PacketCount > 0;
    std::ostringstream out;
    out << "selected slot " << (std::clamp(slot, 0, 2) + 1) << "; character packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::CreateCharacter(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance, int32 timeoutMs)
{
    FCharacterActionResult result;

    if (!Connected()) { result.Message = "character session is closed"; return result; }

    const auto packet = EncodeClientPacketForSphereEmu(BuildCreateCharacterPacket(Impl->LocalId, slot, name, appearance));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character create send failed"); return result; }

    bool namePassed = false;
    bool nameExists = false;
    std::vector<uint8> frame;

    for (int32 i = 0; i < 6 && RecvFrame(Impl->Socket.Get(), frame, timeoutMs); ++i)
    {
        ++result.PacketCount;
        result.ByteCount += static_cast<int32>(frame.size());
        result.Frames.push_back(frame);

        if (LooksLikeNameAlreadyExists(frame)) { nameExists = true; break; }

        if (LooksLikeNameCheckPassed(frame)) { namePassed = true; ReadAvailableFrames(Impl->Socket.Get(), result, 6); break; }
    }

    result.Ok = namePassed && !nameExists;
    std::ostringstream out;

    if (nameExists)
    {
        out << "character name already exists";
    }
    else if (namePassed)
    {
        out << "created slot " << (std::clamp(slot, 0, 2) + 1);
    }
    else
    {
        out << "character create was not acknowledged";
    }

    out << "; packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::DeleteCharacter(int32 slot, int32 timeoutMs)
{
    FCharacterActionResult result;

    if (!Connected()) { result.Message = "character session is closed"; return result; }

    const auto packet = EncodeClientPacketForSphereEmu(BuildDeleteCharacterPacket(Impl->LocalId, slot));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character delete send failed"); return result; }

    ReadActionFrames(Impl->Socket.Get(), result, 1, std::max(100, timeoutMs / 5));
    result.Ok = true;
    result.Disconnected = result.PacketCount == 0;
    std::ostringstream out;
    out << "delete sent for slot " << (std::clamp(slot, 0, 2) + 1);

    if (result.Disconnected)
    {
        out << "; server closed character session";
    }
    else
    {
        out << "; packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    }

    Impl->Socket.Reset();
    result.Disconnected = true;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::SendIngameAck(int32 timeoutMs)
{
    FCharacterActionResult result;

    if (!Connected()) { result.Message = "character session is closed"; return result; }

    const auto packet = EncodeClientPacketForSphereEmu(BuildIngameAckPacket(Impl->LocalId));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("ingame ACK send failed"); return result; }

    ReadActionFrames(Impl->Socket.Get(), result, 8, timeoutMs);
    result.Ok = true;
    std::ostringstream out;
    out << "ingame ACK sent; world packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::PollFrames(int32 maxFrames)
{
    FCharacterActionResult result;

    if (!Connected()) { result.Message = "character session is closed"; return result; }

    ReadAvailableFrames(Impl->Socket.Get(), result, std::max(1, maxFrames));
    result.Ok = true;
    return result;
}

bool FServerSession::SendPosition(double x, double y, double z, double angle, std::string& error)
{
    if (!Connected()) { error = "character session is closed"; return false; }

    const auto packet = BuildPositionPacket(Impl->LocalId, ++Impl->PositionSequence, x, y, z, angle);

    if (!SendAll(Impl->Socket.Get(), packet)) { error = WsaErrorText("position send failed"); return false; }

    return true;
}

FLoginProbeResult ProbeLoginServer(const FEndpoint& endpoint, const std::wstring& login, const std::wstring& password, const FCharacterAppearanceRules& appearanceRules, int32 timeoutMs)
{
    FLoginProbeResult result;
    auto impl = std::make_unique<FServerSession::FImpl>();

    if (!impl->Wsa.Ok()) { result.Message = WsaErrorText("WSAStartup failed"); return result; }

    std::string error;
    impl->Socket = ConnectSocket(endpoint.Host, endpoint.Port, timeoutMs, error);

    if (!impl->Socket) { result.Message = error; return result; }

    result.Connected = true;
    SOCKET socket = impl->Socket.Get();
    std::vector<uint8> firstFrame;

    if (!RecvFrame(socket, firstFrame, timeoutMs)) { result.Message = "TCP connected; no initial server frame"; return result; }

    result.FirstLength = static_cast<int32>(firstFrame.size());
    result.FirstOpcode = ReadU16Le(firstFrame, 2);

    if (result.FirstOpcode == 100) { result.Message = "Server refused connection: connection limit"; return result; }

    if (result.FirstOpcode != 200)
    {
        std::ostringstream out;
        out << "TCP connected; unexpected first opcode " << result.FirstOpcode << " len=" << result.FirstLength;
        result.Message = out.str();
        return result;
    }

    result.LegacyHandshake = true;
    const uint16 xorKey = ReadU16Le(firstFrame, 8);
    uint16 sequence = static_cast<uint16>((std::rand() % 1000) + 1);
    const std::vector<uint8> connectionAck
    {
        3, 0, 0, 0
    };
    const auto ackPacket = BuildLegacyPacket(400, connectionAck, sequence, xorKey);

    if (!SendAll(socket, ackPacket)) { result.Message = WsaErrorText("legacy ACK send failed"); return result; }

    std::vector<uint8> nextFrame;

    if (RecvFrame(socket, nextFrame, timeoutMs))
    {
        result.NextLength = static_cast<int32>(nextFrame.size());
        result.NextOpcode = ReadU16Le(nextFrame, 2);
    }

    if (result.NextOpcode == 300 && nextFrame.size() >= 13 && !login.empty() && !password.empty())
    {
        Sleep(250);
        const auto localId = static_cast<uint16>((nextFrame[7] << 8) | nextFrame[8]);
        const auto loginPacket = EncodeClientPacketForSphereEmu(BuildSphereEmuLoginPacket(localId, login, password));

        if (!SendAll(socket, loginPacket)) { result.Message = WsaErrorText("login packet send failed"); return result; }

        std::vector<uint8> loginResponse;

        if (RecvFrame(socket, loginResponse, timeoutMs * 3))
        {
            result.NextLength = static_cast<int32>(loginResponse.size());
            result.NextOpcode = ReadU16Le(loginResponse, 2);

            if (LooksLikeCannotConnect(loginResponse))
            {
                std::ostringstream out;
                out << "Login rejected by server; localId=0x" << std::hex << localId << std::dec;
                result.Message = out.str();
                return result;
            }

            if (LooksLikeCharacterSelectStart(loginResponse))
            {
                int32 extraFrames = 0;
                int32 extraBytes = 0;
                int32 slotFrames = 0;
                std::vector<uint8> extraFrame;

                while (extraFrames < 8 && slotFrames < 3 && RecvFrame(socket, extraFrame, 500))
                {
                    ++extraFrames;
                    extraBytes += static_cast<int32>(extraFrame.size());

                    if (LooksLikeCharacterSlot(extraFrame))
                    {
                        result.CharacterSlots[static_cast<size_t>(slotFrames)] = ParseCharacterSlot(extraFrame, slotFrames, appearanceRules);
                        ++slotFrames;
                    }
                }

                result.CharacterSelectReady = slotFrames == 3;
                result.LocalId = localId;
                result.CharacterSelectPackets = extraFrames;
                result.CharacterSelectBytes = extraBytes;
                impl->LocalId = localId;
                result.Session = std::shared_ptr<FServerSession>(new FServerSession(std::move(impl)));
                std::ostringstream out;
                out << "Login OK; character select data received; localId=0x" << std::hex << localId << std::dec << "; packets=" << extraFrames << " bytes=" << extraBytes << "; slots=" << slotFrames;
                result.Message = out.str();
                return result;
            }

            std::ostringstream out;
            out << "Encoded login sent; localId=0x" << std::hex << localId << std::dec << "; server opcode=" << result.NextOpcode << " len=" << result.NextLength;
            result.Message = out.str();
            return result;
        }

        std::ostringstream out;
        out << "Encoded login sent; localId=0x" << std::hex << localId << std::dec << "; waiting for server response";
        result.Message = out.str();
        return result;
    }

    std::ostringstream out;
    out << "Legacy TCP handshake OK";

    if (result.NextOpcode != 0)
    {
        out << "; next opcode=" << result.NextOpcode << " len=" << result.NextLength;
    }
    else
    {
        out << "; waiting for server data";
    }

    result.Message = out.str();
    return result;
}
