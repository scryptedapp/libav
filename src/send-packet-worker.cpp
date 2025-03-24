#include "send-packet-worker.h"

SendPacketWorker::SendPacketWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext, AVPacketObject *packet)
    : Napi::AsyncWorker(env), result(false), deferred(deferred), codecContext(codecContext), packet(packet)
{
}

void SendPacketWorker::Execute() {
    result = false;

    if (!packet || !packet->packet) {
        SetError("SendPacket received null packet");
        return;
    }

    int ret = avcodec_send_packet(codecContext->codecContext, packet->packet);

    if (!ret) {
        result = true;
        return;
    }

    if (ret != AVERROR(EAGAIN)) {
        // errors are typically caused by missing codec info
        // or invalid packets, and may resolve on a later packet.
        // suppress all errors until a keyframe is sent.
        SetError(AVErrorString(ret));
    }
}

void SendPacketWorker::OnOK() {
    Napi::Env env = Env();
    napi_resolve_deferred(env, deferred, Napi::Boolean::New(env, result));
}

void SendPacketWorker::OnError(const Napi::Error &e) {
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
