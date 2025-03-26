#pragma once

#include <napi.h>

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavcodec/bsf.h>
}

#include "error.h"

class AVBitstreamFilterObject : public Napi::ObjectWrap<AVBitstreamFilterObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);

    AVBitstreamFilterObject(const Napi::CallbackInfo &info);
    ~AVBitstreamFilterObject();

    static Napi::Object NewInstance(Napi::Env env, AVBSFContext *bsfContext);
    AVBSFContext *bsfContext;

private:
    static Napi::FunctionReference constructor;

    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value SetOption(const Napi::CallbackInfo &info);
    Napi::Value SendPacket(const Napi::CallbackInfo &info);
    Napi::Value ReceivePacket(const Napi::CallbackInfo &info);
    Napi::Value CopyParameters(const Napi::CallbackInfo &info);
};
