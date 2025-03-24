#include "receive-packet-worker.h"

ReceivePacketWorker::ReceivePacketWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext)
    : Napi::AsyncWorker(env), result(nullptr), deferred(deferred), codecContext(codecContext)
{
}

void ReceivePacketWorker::Execute() {
    result = nullptr;

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        SetError("Failed to allocate packet");
        return;
    }

    // EAGAIN will be returned if frames needs to be sent to encoder
    int ret = avcodec_receive_packet(codecContext, packet);
    if (!ret) {
        result = packet;
        return;
    }

    av_packet_free(&packet);
    if (ret != AVERROR(EAGAIN)) {
        SetError(AVErrorString(ret));
    }
}

void ReceivePacketWorker::OnOK() {
    Napi::Env env = Env();
    if (!result) {
        napi_resolve_deferred(Env(), deferred, env.Undefined());
    } else {
        napi_resolve_deferred(Env(), deferred, AVPacketObject::NewInstance(env, result));
    }
}

void ReceivePacketWorker::OnError(const Napi::Error &e) {
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
