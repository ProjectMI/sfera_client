#include "Network/TcpSocket.h"
#include <bit>
#include <memory>
#include <WinSock2.h>
#include <WS2tcpip.h>


struct FNativeTcpSocket
{
    SOCKET Handle = INVALID_SOCKET;

    ~FNativeTcpSocket()
    {
        Reset();
    }

    void Reset(SOCKET next = INVALID_SOCKET)
    {
        if (Handle != INVALID_SOCKET)
        {
            closesocket(Handle);
        }

        Handle = next;
    }
};

static FStatus EnsureWinsock()
{
    static bool initialized = false;

    if (initialized) { return FStatus::Ok(); }

    WSADATA data{};
    int rc = WSAStartup(MAKEWORD(2, 2), &data);

    if (rc != 0) { return FStatus::Error(EStatusCode::NetworkError, "WSAStartup failed: " + std::to_string(rc)); }

    initialized = true;
    return FStatus::Ok();
}

FTcpSocket::FTcpSocket() : Native(std::make_unique<FNativeTcpSocket>()) {}
FTcpSocket::~FTcpSocket()
{
    Close();
}

FStatus FTcpSocket::Connect(const FEndpoint& endpoint) { return Connect(endpoint, 0); }

FStatus FTcpSocket::Connect(const FEndpoint& endpoint, uint32 timeoutMs)
{
    FStatus ws = EnsureWinsock();

    if (!ws.IsOk()) { return ws; }

    Close();
    State = EConnectionState::Resolving;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int rc = getaddrinfo(endpoint.Host.c_str(), std::to_string(endpoint.Port).c_str(), &hints, &result);

    if (rc != 0 || !result) { State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, "getaddrinfo failed for " + endpoint.Host); }

    State = EConnectionState::Connecting;
    SOCKET socketHandle = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (socketHandle == INVALID_SOCKET) { freeaddrinfo(result); State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, "socket creation failed"); }

    bool useTimeout = timeoutMs > 0;

    if (useTimeout)
    {
        u_long enabled = 1;
        ioctlsocket(socketHandle, FIONBIO, &enabled);
    }

    rc = connect(socketHandle, result->ai_addr, static_cast<int>(result->ai_addrlen));

    if (rc != 0)
    {
        int err = WSAGetLastError();

        if (!useTimeout || (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEINVAL)) { closesocket(socketHandle); freeaddrinfo(result); State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, "connect failed: " + std::to_string(err)); }

        fd_set writeSet{};
        FD_ZERO(&writeSet);
        FD_SET(socketHandle, &writeSet);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeoutMs / 1000);
        tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
        rc = select(0, nullptr, &writeSet, nullptr, &tv);

        if (rc <= 0) { closesocket(socketHandle); freeaddrinfo(result); State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, rc == 0 ? "connect timeout" : "connect select failed"); }

        int soError = 0;
        int soLen = sizeof(soError);
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, std::bit_cast<char*>(&soError), &soLen);

        if (soError != 0) { closesocket(socketHandle); freeaddrinfo(result); State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, "connect failed: " + std::to_string(soError)); }
    }

    freeaddrinfo(result);

    if (useTimeout)
    {
        u_long disabled = 0;
        ioctlsocket(socketHandle, FIONBIO, &disabled);
    }

    Native->Reset(socketHandle);
    State = EConnectionState::Connected;
    return FStatus::Ok();
}

FStatus FTcpSocket::SetNonBlocking(bool enabled)
{
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "nonblocking mode on closed socket"); }

    u_long value = enabled ? 1UL : 0UL;
    int rc = ioctlsocket(Native->Handle, FIONBIO, &value);
    return rc == 0 ? FStatus::Ok() : FStatus::Error(EStatusCode::NetworkError, "ioctlsocket(FIONBIO) failed: " + std::to_string(WSAGetLastError()));
}

FStatus FTcpSocket::Send(const FByteArray& bytes)
{
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "send on closed socket"); }

    size_t offset = 0;

    while (offset < bytes.size())
    {
        int sent = send(Native->Handle, std::bit_cast<const char*>(bytes.data() + offset), static_cast<int>(bytes.size() - offset), 0);

        if (sent <= 0) { return FStatus::Error(EStatusCode::NetworkError, "send failed: " + std::to_string(WSAGetLastError())); }

        offset += static_cast<size_t>(sent);
    }

    return FStatus::Ok();
}

TResult<FByteArray> FTcpSocket::Receive(size_t maxBytes)
{
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "receive on closed socket"); }

    FByteArray bytes(maxBytes);
    int received = recv(Native->Handle, std::bit_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);

    if (received <= 0) { return FStatus::Error(EStatusCode::NetworkError, "receive failed or closed: " + std::to_string(WSAGetLastError())); }

    bytes.resize(static_cast<size_t>(received));
    return bytes;
}

TResult<FByteArray> FTcpSocket::ReceiveAvailable(size_t maxBytes)
{
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "receive on closed socket"); }

    FByteArray bytes(maxBytes);
    int received = recv(Native->Handle, std::bit_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);

    if (received == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAEWOULDBLOCK) { return FByteArray{}; }

        return FStatus::Error(EStatusCode::NetworkError, "receive failed: " + std::to_string(err));
    }

    if (received == 0) { Close(); return FByteArray{}; }

    bytes.resize(static_cast<size_t>(received));
    return bytes;
}

void FTcpSocket::Close()
{
    if (Native)
    {
        Native->Reset();
    }

    State = EConnectionState::Closed;
}
