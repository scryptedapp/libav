#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "../frame.h"
#include "../codeccontext.h"

class SendFrameWorker : public Napi::AsyncWorker {
public:
    SendFrameWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext, AVFrameObject *frame);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    bool result;
    napi_deferred deferred;
    AVCodecContextObject *codecContext;
    AVFrameObject *frame;
};
