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

    AVFilterGraphObject(const Napi::CallbackInfo &info);
    ~AVFilterGraphObject();

    std::vector<AVFilterContext*> buffersrc_ctxs;
    std::vector<AVFilterContext*> buffersink_ctxs;

private:
    static Napi::FunctionReference constructor;

    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value SendCommand(const Napi::CallbackInfo &info);
    Napi::Value AddFrame(const Napi::CallbackInfo &info);
    Napi::Value GetFrame(const Napi::CallbackInfo &info);

    AVFilterGraph *filterGraph;
};
