#include "send-frame-worker.h"
#include "../error.h"

SendFrameWorker::SendFrameWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext, AVFrameObject *frame)
    : Napi::AsyncWorker(env), result(false), deferred(deferred), codecContext(codecContext), frame(frame)
{
}

void SendFrameWorker::Execute() {
    result = false;

    if (!frame || !frame->frame_) {
        SetError("SendFrame received null frame");
        return;
    }
    if (!codecContext || !codecContext->codecContext) {
        SetError("Codec Context is null");
        return;
    }

    int ret = avcodec_send_frame(codecContext->codecContext, frame->frame_);

    if (!ret) {
        result = true;
        return;
    }

    if (ret != AVERROR(EAGAIN)) {
        SetError(AVErrorString(ret));
    }
}

void SendFrameWorker::OnOK() {
    Napi::Env env = Env();
    napi_resolve_deferred(env, deferred, Napi::Boolean::New(env, result));
}

void SendFrameWorker::OnError(const Napi::Error &e) {
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
