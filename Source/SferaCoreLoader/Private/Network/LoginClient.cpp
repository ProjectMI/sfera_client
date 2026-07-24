#include "Network/LoginClient.h"
#include "Common/ValueUtils.h"
#include "Common/TextEncoding.h"

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

    std::string WsaErrorText(std::string_view prefix)
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
                    getsockopt(socketHandle.Get(), SOL_SOCKET, SO_ERROR, std::bit_cast<char*>(&socketError), &socketErrorSize);

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

            const int rc = recv(socket, std::bit_cast<char*>(out + total), size - total, 0);

            if (rc <= 0) { return false; }

            total += rc;
        }

        return true;
    }

    bool RecvFrame(SOCKET socket, std::vector<uint8>& frame, int32 timeoutMs)
    {
        std::array<uint8, 2> header{};

        if (!RecvExact(socket, header.data(), static_cast<int>(header.size()), timeoutMs)) { return false; }

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
            const int rc = send(socket, std::bit_cast<const char*>(data.data() + sent), static_cast<int>(data.size()) - sent, 0);

            if (rc <= 0) { return false; }

            sent += rc;
        }

        return true;
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
            if (ioctlsocket(socket, FIONREAD, &available) != 0) { result.Disconnected = true; break; }
            if (available < 2)
            {
                if (available == 0)
                {
                    fd_set readable;
                    FD_ZERO(&readable);
                    FD_SET(socket, &readable);
                    timeval immediate{};
                    if (select(0, &readable, nullptr, nullptr, &immediate) > 0 && FD_ISSET(socket, &readable))
                    {
                        char probe = 0;
                        const int rc = recv(socket, &probe, 1, MSG_PEEK);
                        if (rc == 0 || (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) { result.Disconnected = true; }
                    }
                }
                break;
            }

            std::array<uint8, 2> header{};
            const int peeked = recv(socket, std::bit_cast<char*>(header.data()), static_cast<int>(header.size()), MSG_PEEK);
            if (peeked != static_cast<int>(header.size()))
            {
                if (peeked == 0 || (peeked < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) { result.Disconnected = true; }
                break;
            }

            const int32 length = header[0] | (header[1] << 8);
            if (length < 4 || length > 60000) { result.Disconnected = true; break; }
            if (available < static_cast<u_long>(length)) { break; }

            std::vector<uint8> frame;
            if (!RecvFrame(socket, frame, 0)) { result.Disconnected = true; break; }

            ++result.PacketCount;
            result.ByteCount += static_cast<int32>(frame.size());
            result.Frames.push_back(std::move(frame));
        }
    }


    void ReadActionFramesBurst(SOCKET socket, FCharacterActionResult& result, int32 maxFrames, int32 firstTimeoutMs)
    {
        std::vector<uint8> frame;
        if (result.PacketCount < maxFrames && RecvFrame(socket, frame, firstTimeoutMs))
        {
            ++result.PacketCount;
            result.ByteCount += static_cast<int32>(frame.size());
            result.Frames.push_back(std::move(frame));
        }
        ReadAvailableFrames(socket, result, maxFrames);
    }


}

struct FServerSession::FImpl
{
    FWsaSession Wsa;
    FSocketHandle Socket;
    uint16 LocalId = 0;
    mutable std::mutex IoMutex;
};

FServerSession::FServerSession(std::unique_ptr<FImpl> impl) : Impl(std::move(impl)) {}
std::shared_ptr<FServerSession> FServerSession::Create(std::unique_ptr<FImpl> impl)
{
    struct FServerSessionInstance : FServerSession
    {
        explicit FServerSessionInstance(std::unique_ptr<FImpl> impl) : FServerSession(std::move(impl)) {}
    };

    return std::make_shared<FServerSessionInstance>(std::move(impl));
}
FServerSession::~FServerSession() = default;
bool FServerSession::Connected() const
{
    if (!Impl) { return false; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);
    return static_cast<bool>(Impl->Socket);
}
uint16 FServerSession::LocalId() const { return Impl ? Impl->LocalId : 0; }
void FServerSession::Close()
{
    if (!Impl) { return; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);
    Impl->Socket.Reset();
}

FCharacterActionResult FServerSession::SelectCharacter(int32 slot, int32 timeoutMs)
{
    FCharacterActionResult result;
    if (!Impl) { result.Message = "character session is closed"; return result; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);

    if (!Impl->Socket) { result.Message = "character session is closed"; return result; }

    const auto packet = FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildCharacterSelectPacket(Impl->LocalId, slot));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character select send failed"); return result; }

    ReadActionFramesBurst(Impl->Socket.Get(), result, 16, timeoutMs);
    result.Ok = result.PacketCount > 0;
    std::ostringstream out;
    out << "selected slot " << (Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1) << "; character packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::CreateCharacter(int32 slot, const std::wstring& name, const FCharacterCreationAppearance& appearance, int32 timeoutMs)
{
    FCharacterActionResult result;
    if (!Impl) { result.Message = "character session is closed"; return result; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);

    if (!Impl->Socket) { result.Message = "character session is closed"; return result; }

    const auto packet = FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildCreateCharacterPacket(Impl->LocalId, slot, name, appearance));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character create send failed"); return result; }

    bool namePassed = false;
    bool nameExists = false;
    std::vector<uint8> frame;

    for (int32 i = 0; i < 6 && RecvFrame(Impl->Socket.Get(), frame, timeoutMs); ++i)
    {
        ++result.PacketCount;
        result.ByteCount += static_cast<int32>(frame.size());
        result.Frames.push_back(frame);

        if (FSphereEmuProtocol::LooksLikeNameAlreadyExists(frame)) { nameExists = true; break; }

        if (FSphereEmuProtocol::LooksLikeNameCheckPassed(frame)) { namePassed = true; ReadAvailableFrames(Impl->Socket.Get(), result, 6); break; }
    }

    result.Ok = namePassed && !nameExists;
    std::ostringstream out;

    if (nameExists)
    {
        out << "character name already exists";
    }
    else if (namePassed)
    {
        out << "created slot " << (Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1);
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
    if (!Impl) { result.Message = "character session is closed"; return result; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);

    if (!Impl->Socket) { result.Message = "character session is closed"; return result; }

    const auto packet = FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildDeleteCharacterPacket(Impl->LocalId, slot));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("character delete send failed"); return result; }

    ReadActionFrames(Impl->Socket.Get(), result, 1, std::max(100, timeoutMs / 5));
    result.Ok = true;
    result.Disconnected = result.PacketCount == 0;
    std::ostringstream out;
    out << "delete sent for slot " << (Common::ClampIndexToCount(slot, Sfera::CharacterSlotCount) + 1);

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
    if (!Impl) { result.Message = "character session is closed"; return result; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);

    if (!Impl->Socket) { result.Message = "character session is closed"; return result; }

    const auto packet = FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildIngameAckPacket(Impl->LocalId));

    if (!SendAll(Impl->Socket.Get(), packet)) { result.Message = WsaErrorText("ingame ACK send failed"); return result; }

    ReadActionFramesBurst(Impl->Socket.Get(), result, 32, timeoutMs);
    result.Ok = true;
    std::ostringstream out;
    out << "ingame ACK sent; world packets=" << result.PacketCount << " bytes=" << result.ByteCount;
    result.Message = out.str();
    return result;
}

FCharacterActionResult FServerSession::PollFrames(int32 maxFrames)
{
    FCharacterActionResult result;
    if (!Impl) { result.Message = "character session is closed"; return result; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);

    if (!Impl->Socket) { result.Message = "character session is closed"; return result; }

    ReadAvailableFrames(Impl->Socket.Get(), result, std::max(1, maxFrames));
    result.Ok = true;
    return result;
}
bool FServerSession::SendChatMessage(uint8 channel, std::string_view utf8Text)
{
    if (!Impl || utf8Text.empty()) { return false; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);
    if (!Impl->Socket) { return false; }
    FByteArray text = FSphereEmuProtocol::ToCp1251(Common::Utf8ToWide(std::string(utf8Text)));
    if (text.empty() || text.size() > 4096) { return false; }
    const uint16 partCount = static_cast<uint16>((text.size() + 249) / 250);
    FByteArray header(8, 0);
    header[0] = channel;
    header[1] = 1;
    const uint32 total = static_cast<uint32>(text.size());
    header[2] = static_cast<uint8>(total & 0xff);
    header[3] = static_cast<uint8>((total >> 8) & 0xff);
    header[4] = static_cast<uint8>((total >> 16) & 0xff);
    header[5] = static_cast<uint8>((total >> 24) & 0xff);
    header[6] = static_cast<uint8>(partCount & 0xff);
    header[7] = static_cast<uint8>((partCount >> 8) & 0xff);
    if (!SendAll(Impl->Socket.Get(), FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildMarshaledPacket(Impl->LocalId, 12, 2, header)))) { return false; }
    for (uint16 part = 0; part < partCount; ++part)
    {
        const size_t offset = static_cast<size_t>(part) * 250;
        const size_t count = std::min<size_t>(250, text.size() - offset);
        FByteArray payload(9 + count, 0);
        payload[0] = channel;
        payload[1] = 2;
        payload[2] = static_cast<uint8>(part & 0xff);
        payload[3] = static_cast<uint8>((part >> 8) & 0xff);
        std::copy_n(text.begin() + static_cast<std::ptrdiff_t>(offset), count, payload.begin() + 4);
        if (!SendAll(Impl->Socket.Get(), FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildMarshaledPacket(Impl->LocalId, 12, 2, payload)))) { return false; }
    }
    return true;
}

bool FServerSession::SendStatAllocation(const std::array<int32, 8>& deltas)
{
    if (!Impl) { return false; }
    std::lock_guard<std::mutex> lock(Impl->IoMutex);
    if (!Impl->Socket) { return false; }
    FByteArray payload(32, 0);
    for (size_t index = 0; index < deltas.size(); ++index)
    {
        const uint32 value = static_cast<uint32>(deltas[index]);
        const size_t offset = index * 4;
        payload[offset] = static_cast<uint8>(value & 0xff);
        payload[offset + 1] = static_cast<uint8>((value >> 8) & 0xff);
        payload[offset + 2] = static_cast<uint8>((value >> 16) & 0xff);
        payload[offset + 3] = static_cast<uint8>((value >> 24) & 0xff);
    }
    return SendAll(Impl->Socket.Get(), FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildMarshaledPacket(Impl->LocalId, 12, 17, payload)));
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
    result.Frames.push_back(firstFrame);

    result.FirstLength = static_cast<int32>(firstFrame.size());
    result.FirstOpcode = FSphereEmuProtocol::ReadU16LE(firstFrame, 2);

    if (result.FirstOpcode == SferaProtocol::LegacyConnectionLimitOpcode) { result.Message = "Server refused connection: connection limit"; return result; }

    if (result.FirstOpcode != SferaProtocol::LegacyHandshakeOpcode)
    {
        std::ostringstream out;
        out << "TCP connected; unexpected first opcode " << result.FirstOpcode << " len=" << result.FirstLength;
        result.Message = out.str();
        return result;
    }

    result.LegacyHandshake = true;
    const uint16 xorKey = FSphereEmuProtocol::ReadU16LE(firstFrame, 8);
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> sequenceSeed(1, 1000);
    uint16 sequence = static_cast<uint16>(sequenceSeed(rng));
    const std::vector<uint8> connectionAck
    {
        3, 0, 0, 0
    };
    const auto ackPacket = FSphereEmuProtocol::BuildLegacyPacket(SferaProtocol::LegacyAckOpcode, connectionAck, sequence, xorKey);

    if (!SendAll(socket, ackPacket)) { result.Message = WsaErrorText("legacy ACK send failed"); return result; }

    std::vector<uint8> nextFrame;

    if (RecvFrame(socket, nextFrame, timeoutMs))
    {
        result.NextLength = static_cast<int32>(nextFrame.size());
        result.NextOpcode = FSphereEmuProtocol::ReadU16LE(nextFrame, 2);
        result.Frames.push_back(nextFrame);
    }

    if (result.NextOpcode == SferaProtocol::ServerFrameOpcode && nextFrame.size() >= 13 && !login.empty() && !password.empty())
    {
        Sleep(250);
        const auto localId = static_cast<uint16>((nextFrame[7] << 8) | nextFrame[8]);
        const auto loginPacket = FSphereEmuProtocol::EncodeClientPacket(FSphereEmuProtocol::BuildLoginPacket(localId, login, password));

        if (!SendAll(socket, loginPacket)) { result.Message = WsaErrorText("login packet send failed"); return result; }

        std::vector<uint8> loginResponse;

        if (RecvFrame(socket, loginResponse, timeoutMs * 3))
        {
            result.Frames.push_back(loginResponse);
            result.NextLength = static_cast<int32>(loginResponse.size());
            result.NextOpcode = FSphereEmuProtocol::ReadU16LE(loginResponse, 2);

            if (FSphereEmuProtocol::LooksLikeCannotConnect(loginResponse))
            {
                std::ostringstream out;
                out << "Login rejected by server; localId=0x" << std::hex << localId << std::dec;
                result.Message = out.str();
                return result;
            }

            if (FSphereEmuProtocol::LooksLikeCharacterSelectStart(loginResponse))
            {
                int32 extraFrames = 0;
                int32 extraBytes = 0;
                int32 slotFrames = 0;
                std::vector<uint8> extraFrame;

                while (extraFrames < 8 && slotFrames < Sfera::CharacterSlotCount && RecvFrame(socket, extraFrame, 500))
                {
                    ++extraFrames;
                    extraBytes += static_cast<int32>(extraFrame.size());
                    result.Frames.push_back(extraFrame);

                    if (FSphereEmuProtocol::LooksLikeCharacterSlot(extraFrame))
                    {
                        result.CharacterSlots[static_cast<size_t>(slotFrames)] = FSphereEmuProtocol::ParseCharacterSlot(extraFrame, slotFrames, appearanceRules);
                        ++slotFrames;
                    }
                }

                result.CharacterSelectReady = slotFrames == Sfera::CharacterSlotCount;
                result.LocalId = localId;
                result.CharacterSelectPackets = extraFrames;
                result.CharacterSelectBytes = extraBytes;
                impl->LocalId = localId;
                result.Session = FServerSession::Create(std::move(impl));
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
