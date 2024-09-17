#pragma once

#include <napi.h>

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

#include "frame.h"
#include "error.h"
#include "filter.h"

class AVFilterGraphObject : public Napi::ObjectWrap<AVFilterGraphObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Object NewInstance(Napi::Env env, AVFilterGraph *filterGraph, AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx);

    AVFilterGraphObject(const Napi::CallbackInfo &info);
    ~AVFilterGraphObject();
    AVFilterContext *buffersrc_ctx;
    AVFilterContext *buffersink_ctx;

private:
    static Napi::FunctionReference constructor;

    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value Filter(const Napi::CallbackInfo &info);
    Napi::Value SetCrop(const Napi::CallbackInfo &info);

    AVFilterGraph *filterGraph;
};
