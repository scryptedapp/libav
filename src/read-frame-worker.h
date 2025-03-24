#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include "error.h"
#include "frame.h"
#include "packet.h"
#include "formatcontext.h"
#include "codeccontext.h"

class ReadFrameWorker : public Napi::AsyncWorker {
public:
    ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, AVCodecContextObject *codecContextObject, int streamIndex);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
    AVCodecContextObject *codecContextObject;
    int streamIndex;
    AVPacket *packetResult;
    AVFrame *frameResult;
};
