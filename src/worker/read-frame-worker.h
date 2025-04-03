#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <map>
#include "../formatcontext.h"
#include "../codeccontext.h"

class ReadFrameWorker : public Napi::AsyncWorker {
public:
    ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, const std::map<int, AVCodecContextObject*>& codecContextMap);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
    std::map<int, AVCodecContextObject*> codecContextMap;
    AVPacket *packetResult;
    AVFrame *frameResult;
    int frameStreamIndex; // Track which stream the frame came from
};
