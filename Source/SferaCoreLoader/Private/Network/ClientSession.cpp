#include "Network/ClientSession.h"
#include "Network/ClientProtocol.h"
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace Sfera {
namespace {
std::string DescribeSlot(const FCharacterSlotInfo& slot) {
    if (!slot.Present) { return "slot" + std::to_string(slot.Slot) + (slot.CanCreate ? ":empty" : ":locked"); }
    return "slot" + std::to_string(slot.Slot) + ":" + slot.Name + " hp=" + std::to_string(slot.CurrentHp) + "/" + std::to_string(slot.MaxHp);
}
}

FClientSession::FClientSession(FLogger* logger) : Log(logger) {}

const char* FClientSession::StageName(EClientSessionStage stage) {
    switch (stage) {
    case EClientSessionStage::Idle: return "idle";
    case EClientSessionStage::EndpointResolved: return "endpoint_resolved";
    case EClientSessionStage::Connecting: return "connecting";
    case EClientSessionStage::Connected: return "connected";
    case EClientSessionStage::Handshaking: return "handshaking";
    case EClientSessionStage::LoginSent: return "login_sent";
    case EClientSessionStage::CharacterSelectReady: return "character_select_ready";
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
    PendingBytes.clear();
    if (State.Endpoint) { SetStage(EClientSessionStage::EndpointResolved, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port)); }
    else { SetStage(EClientSessionStage::Idle, "no endpoint configured"); }
}

bool FClientSession::ReceiveFrameBlocking(FByteArray& frame, uint32 timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (PendingBytes.size() >= 2) {
            const size_t length = size_t(PendingBytes[0] | (PendingBytes[1] << 8));
            if (length >= 4 && length <= 60000 && PendingBytes.size() >= length) {
                frame.assign(PendingBytes.begin(), PendingBytes.begin() + static_cast<std::ptrdiff_t>(length));
                PendingBytes.erase(PendingBytes.begin(), PendingBytes.begin() + static_cast<std::ptrdiff_t>(length));
                ++State.FramesReceived;
                State.BytesReceived += frame.size();
                return true;
            }
            if (length < 4 || length > 60000) { PendingBytes.clear(); return false; }
        }
        auto data = Socket.ReceiveAvailable(4096);
        if (data.IsOk() && !data.Value().empty()) { PendingBytes.insert(PendingBytes.end(), data.Value().begin(), data.Value().end()); continue; }
        if (!data.IsOk()) { return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void FClientSession::ApplyLoginResult(const FLoginHandshakeResult& result) {
    State.LegacyHandshake = result.LegacyHandshake;
    State.LoginSent = result.LoginSent;
    State.CharacterSelectReady = result.CharacterSelectReady;
    State.LocalId = result.LocalId;
    State.FirstOpcode = result.FirstOpcode;
    State.NextOpcode = result.NextOpcode;
    State.CharacterSelectPackets = result.CharacterSelectPackets;
    State.CharacterSelectBytes = result.CharacterSelectBytes;
    State.CharacterSlots = result.CharacterSlots;
}

FStatus FClientSession::StartProbe(uint32 timeoutMs, std::string login, std::string password) {
    if (!State.Endpoint) { State.LastError = "endpoint not configured"; SetStage(EClientSessionStage::Failed, State.LastError); return FStatus::Error(EStatusCode::NotFound, State.LastError); }
    SetStage(EClientSessionStage::Connecting, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port));
    FStatus status = Socket.Connect(*State.Endpoint, timeoutMs);
    if (!status.IsOk()) { State.LastError = status.Message(); SetStage(EClientSessionStage::Failed, State.LastError); return status; }
    Socket.SetNonBlocking(true);
    SetStage(EClientSessionStage::Connected, "tcp connected; waiting for legacy hello");

    FLoginHandshakeResult result;
    result.Connected = true;
    FByteArray firstFrame;
    if (!ReceiveFrameBlocking(firstFrame, timeoutMs)) { result.Message = "TCP connected; no initial server frame"; ApplyLoginResult(result); SetStage(EClientSessionStage::Connected, result.Message); return FStatus::Ok(); }
    result.FirstLength = static_cast<int32>(firstFrame.size());
    result.FirstOpcode = ReadU16LE(firstFrame, 2);
    if (result.FirstOpcode == 100) { result.Message = "server refused connection: connection limit"; ApplyLoginResult(result); State.LastError = result.Message; SetStage(EClientSessionStage::Failed, result.Message); return FStatus::Error(EStatusCode::NetworkError, result.Message); }
    if (result.FirstOpcode != 200) { result.Message = "tcp connected; unexpected first opcode " + std::to_string(result.FirstOpcode) + " len=" + std::to_string(result.FirstLength); ApplyLoginResult(result); SetStage(EClientSessionStage::Connected, result.Message); return FStatus::Ok(); }

    SetStage(EClientSessionStage::Handshaking, "server hello opcode 200; sending legacy ACK");
    result.LegacyHandshake = true;
    const uint16 xorKey = ReadU16LE(firstFrame, 8);
    uint16 sequence = static_cast<uint16>((std::rand() % 1000) + 1);
    const FByteArray ackPayload{3, 0, 0, 0};
    FStatus sendStatus = Socket.Send(BuildLegacyPacket(400, ackPayload, sequence, xorKey));
    if (!sendStatus.IsOk()) { State.LastError = sendStatus.Message(); SetStage(EClientSessionStage::Failed, State.LastError); return sendStatus; }

    FByteArray nextFrame;
    if (ReceiveFrameBlocking(nextFrame, timeoutMs)) {
        result.NextLength = static_cast<int32>(nextFrame.size());
        result.NextOpcode = ReadU16LE(nextFrame, 2);
    }

    if (result.NextOpcode == 300 && nextFrame.size() >= 13 && !login.empty() && !password.empty()) {
        const uint16 localId = static_cast<uint16>((nextFrame[7] << 8) | nextFrame[8]);
        result.LocalId = localId;
        const FByteArray loginPacket = EncodeClientPacketForSphereEmu(BuildSphereEmuLoginPacket(localId, login, password));
        sendStatus = Socket.Send(loginPacket);
        if (!sendStatus.IsOk()) { State.LastError = sendStatus.Message(); SetStage(EClientSessionStage::Failed, State.LastError); return sendStatus; }
        result.LoginSent = true;
        SetStage(EClientSessionStage::LoginSent, "encoded login sent; localId=0x" + std::to_string(localId));

        FByteArray loginResponse;
        if (ReceiveFrameBlocking(loginResponse, timeoutMs * 3)) {
            result.NextLength = static_cast<int32>(loginResponse.size());
            result.NextOpcode = ReadU16LE(loginResponse, 2);
            if (LooksLikeCannotConnect(loginResponse)) { result.LoginRejected = true; result.Message = "login rejected by server"; ApplyLoginResult(result); State.LastError = result.Message; SetStage(EClientSessionStage::Failed, result.Message); return FStatus::Error(EStatusCode::NetworkError, result.Message); }
            if (LooksLikeCharacterSelectStart(loginResponse)) {
                int32 extraFrames = 0;
                int32 extraBytes = 0;
                int32 slotFrames = 0;
                FByteArray extraFrame;
                while (extraFrames < 8 && slotFrames < 3 && ReceiveFrameBlocking(extraFrame, 500)) {
                    ++extraFrames;
                    extraBytes += static_cast<int32>(extraFrame.size());
                    if (LooksLikeCharacterSlot(extraFrame)) { result.CharacterSlots[static_cast<size_t>(slotFrames)] = ParseCharacterSlot(extraFrame, slotFrames); ++slotFrames; }
                }
                result.CharacterSelectReady = slotFrames == 3;
                result.CharacterSelectPackets = extraFrames;
                result.CharacterSelectBytes = extraBytes;
                std::ostringstream out;
                out << "character select " << (result.CharacterSelectReady ? "ready" : "partial") << "; localId=0x" << std::hex << result.LocalId << std::dec << "; packets=" << extraFrames << "; slots=" << slotFrames;
                for (const auto& slot : result.CharacterSlots) { out << "; " << DescribeSlot(slot); }
                result.Message = out.str();
                ApplyLoginResult(result);
                SetStage(EClientSessionStage::CharacterSelectReady, result.Message);
                return FStatus::Ok();
            }
        }
        result.Message = "encoded login sent; waiting for character select response";
        ApplyLoginResult(result);
        SetStage(EClientSessionStage::LoginSent, result.Message);
        return FStatus::Ok();
    }

    result.Message = "legacy TCP handshake OK";
    if (result.NextOpcode != 0) { result.Message += "; next opcode=" + std::to_string(result.NextOpcode) + " len=" + std::to_string(result.NextLength); }
    else { result.Message += "; waiting for server data"; }
    ApplyLoginResult(result);
    SetStage(EClientSessionStage::Handshaking, result.Message);
    return FStatus::Ok();
}

void FClientSession::Tick() {
    if (!Socket.IsConnected()) { return; }
    auto data = Socket.ReceiveAvailable(4096);
    if (!data.IsOk()) { return; }
    if (data.Value().empty()) { return; }
    PendingBytes.insert(PendingBytes.end(), data.Value().begin(), data.Value().end());
    FByteArray frame;
    while (PendingBytes.size() >= 2) {
        const size_t length = size_t(PendingBytes[0] | (PendingBytes[1] << 8));
        if (length < 4 || length > 60000) { PendingBytes.clear(); break; }
        if (PendingBytes.size() < length) { break; }
        frame.assign(PendingBytes.begin(), PendingBytes.begin() + static_cast<std::ptrdiff_t>(length));
        PendingBytes.erase(PendingBytes.begin(), PendingBytes.begin() + static_cast<std::ptrdiff_t>(length));
        ++State.FramesReceived;
        State.BytesReceived += frame.size();
        FClientProtocolProbe probe;
        auto probeResult = probe.Inspect(frame);
        if (Log) { Log->Info("ClientSession recv frame: " + FClientProtocolProbe::Describe(probeResult)); }
    }
    SetStage(EClientSessionStage::ProbeReceiving, "received bytes=" + std::to_string(State.BytesReceived) + ", frames=" + std::to_string(State.FramesReceived));
}

void FClientSession::Close() {
    Socket.Close();
    PendingBytes.clear();
    SetStage(EClientSessionStage::Closed, "socket closed");
}
}
