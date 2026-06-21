#include "Network/ClientSession.h"
#include "Network/ClientProtocol.h"

FClientSession::FClientSession(FLogger* logger) : Log(logger) {}

const char* FClientSession::StageName(EClientSessionStage stage)
{
    switch (stage)
    {
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

void FClientSession::SetStage(EClientSessionStage stage, std::string text)
{
    State.Stage = stage;
    State.StageText = std::move(text);

    if (Log)
    {
        Log->Info("ClientSession stage: " + std::string(StageName(stage)) + " - " + State.StageText);
    }
}

void FClientSession::Configure(std::optional<FEndpoint> endpoint)
{
    State.Endpoint = std::move(endpoint);

    if (State.Endpoint)
    {
        SetStage(EClientSessionStage::EndpointResolved, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port));
    }
    else
    {
        SetStage(EClientSessionStage::Idle, "no endpoint configured");
    }
}

FStatus FClientSession::StartProbe(uint32 timeoutMs)
{
    if (!State.Endpoint) { State.LastError = "endpoint not configured"; SetStage(EClientSessionStage::Failed, State.LastError); return FStatus::Error(EStatusCode::NotFound, State.LastError); }

    SetStage(EClientSessionStage::Connecting, State.Endpoint->Host + ":" + std::to_string(State.Endpoint->Port));
    FStatus status = Socket.Connect(*State.Endpoint, timeoutMs);

    if (!status.IsOk()) { State.LastError = status.Message(); SetStage(EClientSessionStage::Failed, State.LastError); return status; }

    Socket.SetNonBlocking(true);
    SetStage(EClientSessionStage::Connected, "tcp connected; protocol probe only, no login packet sent");
    return FStatus::Ok();
}

void FClientSession::Tick()
{
    if (!Socket.IsConnected()) { return; }

    auto data = Socket.ReceiveAvailable(4096);

    if (!data.IsOk()) { return; }

    if (data.Value().empty()) { return; }

    State.BytesReceived += data.Value().size();
    FClientProtocolProbe probe;
    auto probeResult = probe.Inspect(data.Value());

    if (Log)
    {
        Log->Info("ClientSession recv probe: " + FClientProtocolProbe::Describe(probeResult));
    }

    U16Frames.Append(data.Value().data(), data.Value().size());
    FByteArray frame;
    size_t frames = 0;

    while (U16Frames.TryPopFrame(frame))
    {
        ++frames;
        ++State.FramesReceived;
    }

    SetStage(EClientSessionStage::ProbeReceiving, "received bytes=" + std::to_string(State.BytesReceived) + ", u16_frames=" + std::to_string(State.FramesReceived));
}

void FClientSession::Close()
{
    Socket.Close();
    SetStage(EClientSessionStage::Closed, "socket closed");
}
