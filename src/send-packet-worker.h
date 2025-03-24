#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "error.h"
#include "packet.h"
#include "codeccontext.h"

class SendPacketWorker : public Napi::AsyncWorker {
public:
    SendPacketWorker(napi_env env, napi_deferred deferred, AVCodecContextObject *codecContext, AVPacketObject *packet);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    bool result;
    napi_deferred deferred;
    AVCodecContextObject *codecContext;
    AVPacketObject *packet;
};
