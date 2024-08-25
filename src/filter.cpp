#include <napi.h>

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

#include "frame.cpp"

class AVFilterGraphObject : public Napi::ObjectWrap<AVFilterGraphObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFilterGraphObject(const Napi::CallbackInfo &info);
    ~AVFilterGraphObject();
    AVFilterContext *buffersrc_ctx;
    AVFilterContext *buffersink_ctx;

    static Napi::Object NewInstance(Napi::Env env, AVFilterGraph *filterGraph, AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx);

private:
    static Napi::FunctionReference constructor;

    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value Filter(const Napi::CallbackInfo &info);
    Napi::Value SetCrop(const Napi::CallbackInfo &info);

    AVFilterGraph *filterGraph;
};

Napi::FunctionReference AVFilterGraphObject::constructor;

Napi::Object AVFilterGraphObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVFilterGraphObject", {

                                                                      InstanceMethod("destroy", &AVFilterGraphObject::Destroy),

                                                                      InstanceMethod("filter", &AVFilterGraphObject::Filter),

                                                                      InstanceMethod("setCrop", &AVFilterGraphObject::SetCrop)
                                                                  });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    return exports;
}

AVFilterGraphObject::AVFilterGraphObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFilterGraphObject>(info), filterGraph(nullptr)
{
    filterGraph = avfilter_graph_alloc();
    if (!filterGraph)
    {
        Napi::Error::New(info.Env(), "Could not allocate filter graph").ThrowAsJavaScriptException();
    }
}

AVFilterGraphObject::~AVFilterGraphObject()
{
    if (filterGraph)
    {
        avfilter_graph_free(&filterGraph);
        filterGraph = nullptr;
    }
}

Napi::Value AVFilterGraphObject::Destroy(const Napi::CallbackInfo &info)
{
    if (filterGraph)
    {
        avfilter_graph_free(&filterGraph);
        filterGraph = nullptr;
    }
    return info.Env().Undefined();
}

Napi::Value AVFilterGraphObject::SetCrop(const Napi::CallbackInfo &info) {
    // need four arguments
    if (info.Length() < 4)
    {
        Napi::Error::New(info.Env(), "Expected 4 arguments to SetCrop").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    // parse out left, top, width, and height arguments as strings
    if (!info[0].IsString() || !info[1].IsString() || !info[2].IsString() || !info[3].IsString())
    {
        Napi::TypeError::New(info.Env(), "String expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // get the strings from the arguments
    std::string left = info[0].As<Napi::String>().Utf8Value();
    std::string top = info[1].As<Napi::String>().Utf8Value();
    std::string width = info[2].As<Napi::String>().Utf8Value();
    std::string height = info[3].As<Napi::String>().Utf8Value();

    if (avfilter_graph_send_command(filterGraph, "crop", "x", left.c_str(), 0, 0, 0) < 0) {
        Napi::Error::New(info.Env(), "Error while sending crop command (x)").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (avfilter_graph_send_command(filterGraph, "crop", "y", top.c_str(), 0, 0, 0) < 0) {
        Napi::Error::New(info.Env(), "Error while sending crop command (y)").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (avfilter_graph_send_command(filterGraph, "crop", "w", width.c_str(), 0, 0, 0) < 0) {
        Napi::Error::New(info.Env(), "Error while sending crop command (w)").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (avfilter_graph_send_command(filterGraph, "crop", "h", height.c_str(), 0, 0, 0) < 0) {
        Napi::Error::New(info.Env(), "Error while sending crop command (h)").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}

Napi::Value AVFilterGraphObject::Filter(const Napi::CallbackInfo &info)
{
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(info.Env(), "Frame object expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVFrameObject *frameObject = Napi::ObjectWrap<AVFrameObject>::Unwrap(info[0].As<Napi::Object>());

    AVFrame *frame = frameObject->frame_;

    if (!frame)
    {
        Napi::TypeError::New(info.Env(), "Frame object is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (!buffersrc_ctx || !buffersink_ctx)
    {
        Napi::TypeError::New(info.Env(), "Buffersrc or buffersink context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // Feed the frame to the buffer source filter
    int ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        Napi::Error::New(info.Env(), "Error while feeding the filter graph").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // Flush the filter graph
    // ret = av_buffersrc_add_frame_flags(buffersrc_ctx, NULL, AV_BUFFERSRC_FLAG_KEEP_REF);
    // if (ret < 0)
    // {
    //     Napi::Error::New(info.Env(), "Error while flushing the filter graph").ThrowAsJavaScriptException();
    //     return info.Env().Undefined();
    // }

    AVFrame* filtered_frame = av_frame_alloc();
    if (!filtered_frame)
    {
        Napi::Error::New(info.Env(), "Could not allocate filtered frame").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // Get the filtered frame from the buffer sink filter
    ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame);
    if (ret < 0)
    {
        av_frame_free(&filtered_frame);
        Napi::Error::New(info.Env(), "Error while getting the filtered frame").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return AVFrameObject::NewInstance(info.Env(), filtered_frame);
}

// Factory method to create an instance from C++
Napi::Object AVFilterGraphObject::NewInstance(Napi::Env env, AVFilterGraph *filterGraph, AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx)
{
    Napi::Object obj = constructor.New({});
    AVFilterGraphObject *wrapper = Napi::ObjectWrap<AVFilterGraphObject>::Unwrap(obj);
    wrapper->filterGraph = filterGraph;
    wrapper->buffersrc_ctx = buffersrc_ctx;
    wrapper->buffersink_ctx = buffersink_ctx;
    return obj;
}
