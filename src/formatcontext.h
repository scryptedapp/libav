#include <napi.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/avfilter.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
// rtpenc.h header uses a weird restrict keyword
#ifndef restrict
#define restrict
#include <libavformat/rtpenc.h>
#undef restrict
#else
#include <libavformat/rtpenc.h>
#endif
}

#include "openworker.h"

class AVFormatContextObject : public Napi::ObjectWrap<AVFormatContextObject>
{
    friend class OpenWorker;
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFormatContextObject(const Napi::CallbackInfo &info);
    ~AVFormatContextObject(); // Explicitly declare the destructor
    static Napi::FunctionReference constructor;
    AVFormatContext *fmt_ctx_;
    Napi::ThreadSafeFunction callbackRef;

private:
    Napi::Value Open(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value CreateDecoder(const Napi::CallbackInfo &info);
    Napi::Value GetMetadata(const Napi::CallbackInfo &info);
    Napi::Value ReadFrame(const Napi::CallbackInfo &info);
    Napi::Value ReceiveFrame(const Napi::CallbackInfo &info);
    Napi::Value Create(const Napi::CallbackInfo &info);
    Napi::Value NewStream(const Napi::CallbackInfo &info);
    Napi::Value WriteFrame(const Napi::CallbackInfo &info);
    Napi::Value GetStreams(const Napi::CallbackInfo &info);
    Napi::Value CreateSDP(const Napi::CallbackInfo &info);

    bool is_input;
};
