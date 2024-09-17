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

class AVCodecContextObject : public Napi::ObjectWrap<AVCodecContextObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Object NewInstance(Napi::Env env);
    static Napi::FunctionReference constructor;

    AVCodecContextObject(const Napi::CallbackInfo &info);
    ~AVCodecContextObject(); // Explicitly declare the destructor

    enum AVPixelFormat hw_pix_fmt;
    int videoStreamIndex;
    AVCodecContext *codecContext;
    enum AVHWDeviceType hw_device_value;

private:
    Napi::Value GetTimeBaseNum(const Napi::CallbackInfo &info);
    Napi::Value GetTimeBaseDen(const Napi::CallbackInfo &info);
    Napi::Value GetHardwareDevice(const Napi::CallbackInfo &info);
    Napi::Value GetPixelFormat(const Napi::CallbackInfo &info);
    Napi::Value GetHardwarePixelFormat(const Napi::CallbackInfo &info);
    Napi::Value ReceiveFrame(const Napi::CallbackInfo &info);
    Napi::Value ReceivePacket(const Napi::CallbackInfo &info);
    Napi::Value SendPacket(const Napi::CallbackInfo &info);
    Napi::Value SendFrame(const Napi::CallbackInfo &info);
    Napi::Value Destroy(const Napi::CallbackInfo &info);
};
