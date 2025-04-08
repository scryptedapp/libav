#pragma once
#include <napi.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <map>
#include "../formatcontext.h"
#include "../codeccontext.h"
#include "../filter.h"

class ReadFrameWorker : public Napi::AsyncWorker {
public:
    ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, 
                    const std::map<int, AVCodecContextObject*>& decoders,
                    const std::map<int, AVFilterGraphObject*>& filters,
                    const std::map<int, AVCodecContextObject*>& encoders,
                    const std::map<int, AVFormatContextObject*>& writeFormatContexts);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
    std::map<int, AVCodecContextObject*> decoders;
    std::map<int, AVFilterGraphObject*> filters;
    std::map<int, AVCodecContextObject*> encoders;
    std::map<int, AVFormatContextObject*> writeFormatContexts; // New property for write contexts
    AVPacket *packetResult;
    int packetInputStreamIndex;
    AVFrame *frameResult;
    int frameStreamIndex; // Track which stream the frame came from
};
