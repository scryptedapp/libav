#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "error.h"
#include "packet.h"

class ReceivePacketWorker : public Napi::AsyncWorker {
public:
    ReceivePacketWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    AVPacket *result;
    napi_deferred deferred;
    AVCodecContext *codecContext;
};
