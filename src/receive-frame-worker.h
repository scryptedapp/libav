#pragma once
#include <napi.h>
#include "error.h"
#include "frame.h"
#include "codeccontext.h"

class ReceiveFrameWorker : public Napi::AsyncWorker {
public:
    ReceiveFrameWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    AVFrame *result;
    napi_deferred deferred;
    AVCodecContextObject *codecContext;
};
