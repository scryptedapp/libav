#pragma once

#include <napi.h>
extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/avfilter.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

class AVFrameObject : public Napi::ObjectWrap<AVFrameObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFrameObject(const Napi::CallbackInfo &info);
    ~AVFrameObject(); // Explicitly declare the destructor

    // Factory method to create an instance from C++
    static Napi::Object NewInstance(Napi::Env env, AVFrame *frame);
    AVFrame *frame_;

private:
    static Napi::FunctionReference constructor;
    Napi::Value GetWidth(const Napi::CallbackInfo &info);
    Napi::Value GetHeight(const Napi::CallbackInfo &info);
    Napi::Value GetPixelFormat(const Napi::CallbackInfo &info);
    Napi::Value GetTimeBaseNum(const Napi::CallbackInfo &info);
    Napi::Value GetTimeBaseDen(const Napi::CallbackInfo &info);

    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value ToJPEG(const Napi::CallbackInfo &info);
    Napi::Value CreateEncoder(const Napi::CallbackInfo &info);
    Napi::Value ToBuffer(const Napi::CallbackInfo &info);
    Napi::Value CreateFilter(const Napi::CallbackInfo &info);
};