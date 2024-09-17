#pragma once

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
}

#include <thread>
#include <v8.h>

class AVPacketObject : public Napi::ObjectWrap<AVPacketObject>
{
public:
    static Napi::FunctionReference constructor;
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Object NewInstance(Napi::Env env, AVPacket *packet);

    AVPacketObject(const Napi::CallbackInfo &info);
    ~AVPacketObject();
    Napi::Value Clone(const Napi::CallbackInfo &info);
    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value GetIsKeyFrame(const Napi::CallbackInfo &info);
    Napi::Value GetPTS(const Napi::CallbackInfo &info);
    Napi::Value GetDTS(const Napi::CallbackInfo &info);
    Napi::Value GetDuration(const Napi::CallbackInfo &info);
    Napi::Value GetSize(const Napi::CallbackInfo &info);
    Napi::Value GetData(const Napi::CallbackInfo &info);

    AVPacket *packet;
};