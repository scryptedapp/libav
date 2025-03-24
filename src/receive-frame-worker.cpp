#include "receive-frame-worker.h"

ReceiveFrameWorker::ReceiveFrameWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext)
    : Napi::AsyncWorker(env), result(nullptr), deferred(deferred), codecContext(codecContext)
{
}

void ReceiveFrameWorker::Execute() {
    result = nullptr;

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        SetError("Failed to allocate frame");
        return;
    }

    //  EAGAIN will be returned if packets need to be sent to decoder.
    int ret = avcodec_receive_frame(codecContext->codecContext, frame);
    if (!ret) {
        result = frame;
        return;
    }

    av_frame_free(&frame);
    if (ret != AVERROR(EAGAIN)) {
        SetError(AVErrorString(ret));
    }
}

void ReceiveFrameWorker::OnOK() {
    Napi::Env env = Env();
    if (!result) {
        napi_resolve_deferred(Env(), deferred, env.Undefined());
    } else {
        napi_resolve_deferred(Env(), deferred, AVFrameObject::NewInstance(env, result));
    }
}

void ReceiveFrameWorker::OnError(const Napi::Error &e) {
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
