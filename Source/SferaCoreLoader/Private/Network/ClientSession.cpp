#include "Network/ClientSession.h"
#include <iomanip>
#include <sstream>
namespace Sfera {
FClientSession::FClientSession(FLogger* logger) : Log(logger) {}

const char* FClientSession::StageName(EClientSessionStage stage) {
    switch (stage) {
    case EClientSessionStage::Idle: return "idle";
    case EClientSessionStage::EndpointResolved: return "endpoint_resolved";
    case EClientSessionStage::Connecting: return "connecting";
    case EClientSessionStage::Connected: return "connected";
    case EClientSessionStage::ProbeReceiving: return "probe_receiving";
    case EClientSessionStage::Failed: return "failed";
    case EClientSessionStage::Closed: return "closed";
    default: return "unknown";
    }
}

void FClientSession::SetStage(EClientSessionStage stage, std::string text) {
    State.Stage = stage;
    State.StageText = std::move(text);
    if (Log) { Log->Info("ClientSession stage: " + std::string(StageName(stage)) + " - " + State.StageText); }
}

void FClientSession::Configure(std::optional<FEndpoint> endpoint) {
    State.Endpoint = std::move(endpoint);
    if (State.Endpoint) { SetStage(EClientSessionStage::EndpointResolved, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port)); }
    else { SetStage(EClientSessionStage::Idle, "no endpoint configured"); }
}

FStatus FClientSession::StartProbe(uint32 timeoutMs) {
    if (!State.Endpoint) { State.LastError = "endpoint not configured"; SetStage(EClientSessionStage::Failed, State.LastError); return FStatus::Error(EStatusCode::NotFound, State.LastError); }
    SetStage(EClientSessionStage::Connecting, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port));
    FStatus status = Socket.Connect(*State.Endpoint, timeoutMs);
    if (!status.IsOk()) { State.LastError = status.Message(); SetStage(EClientSessionStage::Failed, State.LastError); return status; }
    Socket.SetNonBlocking(true);
    SetStage(EClientSessionStage::Connected, "tcp connected; waiting for Sfera handshake");
    return FStatus::Ok();
}

void FClientSession::Tick() {
    if (!Socket.IsConnected()) { return; }
    auto data = Socket.ReceiveAvailable(4096);
    if (!data.IsOk()) { return; }
    if (data.Value().empty()) { return; }
    State.BytesReceived += data.Value().size();
    FClientProtocolProbe probe;
    auto probeResult = probe.Inspect(data.Value());
    if (Log) { Log->Info("ClientSession recv probe: " + FClientProtocolProbe::Describe(probeResult)); }
    Protocol.Append(data.Value().data(), data.Value().size());
    FSferaNetPacket packet;
    size_t packets = 0;
    while (Protocol.TryPopPacket(packet)) {
        ++packets;
        ++State.FramesReceived;
        State.LastOpcode = packet.Opcode;
        if (packet.Opcode == FSferaNetProtocol::ServerCreateConnection && packet.Payload.size() >= 2) {
            State.ConnectionId = static_cast<uint16>(packet.Payload[0] | (packet.Payload[1] << 8));
            if (packet.Payload.size() >= 4) { State.SessionToken = static_cast<uint16>(packet.Payload[2] | (packet.Payload[3] << 8)); }
            FByteArray payload(4, 0);
            FByteArray ack = Protocol.MakePacket(FSferaNetProtocol::ClientCreateConnectionAck, payload, State.SessionToken);
            FStatus sendStatus = Socket.Send(ack);
            if (sendStatus.IsOk()) { ++State.PacketsSent; }
            else { State.LastError = sendStatus.Message(); if (Log) { Log->Warning("ClientSession handshake ack failed: " + State.LastError); } }
        } else if (packet.Opcode == FSferaNetProtocol::ServerPing) {
            FByteArray pong = Protocol.MakePacket(FSferaNetProtocol::ServerPing, {}, State.SessionToken);
            if (Socket.Send(pong).IsOk()) { ++State.PacketsSent; }
        }
    }
    std::ostringstream status;
    status << "received bytes=" << State.BytesReceived << ", packets=" << State.FramesReceived << ", sent=" << State.PacketsSent << ", opcode=0x" << std::hex << std::uppercase << State.LastOpcode;
    SetStage(EClientSessionStage::ProbeReceiving, status.str());
}

void FClientSession::Close() {
    Socket.Close();
    SetStage(EClientSessionStage::Closed, "socket closed");
}
}
