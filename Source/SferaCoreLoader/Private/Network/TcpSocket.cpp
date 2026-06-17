#include "Network/TcpSocket.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>

namespace Sfera {
static FStatus EnsureWinsock() {
    static bool initialized = false;
    if (initialized) { return FStatus::Ok(); }
    WSADATA data{};
    int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) { return FStatus::Error(EStatusCode::NetworkError, "WSAStartup failed: " + std::to_string(rc)); }
    initialized = true;
    return FStatus::Ok();
}

FTcpSocket::FTcpSocket() = default;
FTcpSocket::~FTcpSocket() { Close(); }

FStatus FTcpSocket::Connect(const FEndpoint& endpoint) {
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
    rc = connect(socketHandle, result->ai_addr, static_cast<int>(result->ai_addrlen));
    if (rc != 0) { closesocket(socketHandle); freeaddrinfo(result); State = EConnectionState::Failed; return FStatus::Error(EStatusCode::NetworkError, "connect failed"); }
    freeaddrinfo(result);
    SocketHandle = static_cast<uintptr_t>(socketHandle);
    State = EConnectionState::Connected;
    return FStatus::Ok();
}

FStatus FTcpSocket::Send(const FByteArray& bytes) {
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "send on closed socket"); }
    int sent = send(static_cast<SOCKET>(SocketHandle), reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    return sent == static_cast<int>(bytes.size()) ? FStatus::Ok() : FStatus::Error(EStatusCode::NetworkError, "send failed");
}

TResult<FByteArray> FTcpSocket::Receive(size_t maxBytes) {
    if (!IsConnected()) { return FStatus::Error(EStatusCode::NetworkError, "receive on closed socket"); }
    FByteArray bytes(maxBytes);
    int received = recv(static_cast<SOCKET>(SocketHandle), reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    if (received <= 0) { return FStatus::Error(EStatusCode::NetworkError, "receive failed or closed"); }
    bytes.resize(static_cast<size_t>(received));
    return bytes;
}

void FTcpSocket::Close() {
    if (SocketHandle != ~uintptr_t(0)) { closesocket(static_cast<SOCKET>(SocketHandle)); SocketHandle = ~uintptr_t(0); }
    State = EConnectionState::Closed;
}
}
