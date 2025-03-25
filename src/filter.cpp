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

#include "ffmpeg-graphparser.cpp"

Napi::FunctionReference AVFilterGraphObject::constructor;

Napi::Object AVFilterGraphObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVFilterGraphObject", {

                                                                      InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFilterGraphObject::Destroy),

                                                                      InstanceMethod("destroy", &AVFilterGraphObject::Destroy),

                                                                      InstanceMethod("addFrame", &AVFilterGraphObject::AddFrame),

                                                                      InstanceMethod("getFrame", &AVFilterGraphObject::GetFrame),

                                                                      InstanceMethod("sendCommand", &AVFilterGraphObject::SendCommand)});

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVFilter", func);

    return exports;
}

AVFilterGraphObject::AVFilterGraphObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFilterGraphObject>(info), filterGraph(nullptr)
{
    filterGraph = nullptr;

    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: options").ThrowAsJavaScriptException();
        return;
    }

    // need filter, frames
    Napi::Object options = info[0].As<Napi::Object>();

    Napi::Value filterValue = options.Get("filter");
    if (!filterValue.IsString())
    {
        Napi::TypeError::New(env, "String expected for filter").ThrowAsJavaScriptException();
        return;
    }

    bool autoConvert = false;
    Napi::Value autoConvertValue = options.Get("autoConvert");
    if (autoConvertValue.IsBoolean())
    {
        autoConvert = autoConvertValue.As<Napi::Boolean>().Value();
    }

    std::string hardwareDevice;
    Napi::Value hardwareDeviceValue = options.Get("hardwareDevice");
    if (hardwareDeviceValue.IsString())
    {
        hardwareDevice = hardwareDeviceValue.As<Napi::String>().Utf8Value();
    }

    std::string hardwareDeviceName;
    Napi::Value hardwareDeviceNameValue = options.Get("hardwareDeviceName");
    if (hardwareDeviceNameValue.IsString())
    {
        if (!hardwareDevice.size())
        {
            Napi::TypeError::New(env, "hardwareDevice must be set if hardwareDeviceName is set").ThrowAsJavaScriptException();
            return;
        }
        hardwareDeviceName = hardwareDeviceNameValue.As<Napi::String>().Utf8Value();
    }

    AVBufferRef *hw_device_ctx = NULL;
    Napi::Value hardwareDeviceFrame = options.Get("hardwareDeviceFrame");
    if (hardwareDeviceFrame.IsObject())
    {
        if (hardwareDevice.size())
        {
            Napi::TypeError::New(env, "hardwareDevice must not be set if hardwareDeviceFrame is set").ThrowAsJavaScriptException();
            return;
        }

        AVFrameObject *frameObject = Napi::ObjectWrap<AVFrameObject>::Unwrap(hardwareDeviceFrame.As<Napi::Object>());
        AVFrame *frame = frameObject->frame_;
        if (!frame || !frame->hw_frames_ctx)
        {
            Napi::TypeError::New(env, "Invalid hardwareDeviceFrame").ThrowAsJavaScriptException();
            return;
        }

        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(frame->hw_frames_ctx->data);
        hw_device_ctx = frames_ctx->device_ref;
    }
    else if (hardwareDevice.length())
    {
        // get hardware device type by string
        enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hardwareDevice.c_str());
        if (type == AV_HWDEVICE_TYPE_NONE)
        {
            Napi::Error::New(env, "Failed to find hardware device type").ThrowAsJavaScriptException();
            return;
        }

        const char *hardwareDeviceNameStr = hardwareDeviceName.length() ? hardwareDeviceName.c_str() : nullptr;
        if (av_hwdevice_ctx_create(&hw_device_ctx, type, hardwareDeviceNameStr, NULL, 0) < 0)
        {
            Napi::Error::New(env, "Failed to create hardware device context").ThrowAsJavaScriptException();
            return;
        }
    }

    Napi::Value framesValue = options.Get("frames");
    if (!framesValue.IsArray())
    {
        Napi::TypeError::New(env, "Array expected for frames").ThrowAsJavaScriptException();
        return;
    }
    std::vector<AVFrame *> frames;
    std::vector<Napi::Object> timeBases;
    Napi::Array framesArray = framesValue.As<Napi::Array>();
    for (unsigned int i = 0; i < framesArray.Length(); i++)
    {
        Napi::Value frameValue = framesArray.Get(i);
        if (!frameValue.IsObject())
        {
            Napi::TypeError::New(env, "Object expected for frames").ThrowAsJavaScriptException();
            return;
        }
        Napi::Value timebaseValue = frameValue.As<Napi::Object>().Get("timeBase");
        if (!timebaseValue.IsObject())
        {
            Napi::TypeError::New(env, "Object expected for timeBase").ThrowAsJavaScriptException();
            return;
        }
        Napi::Object timebaseObject = timebaseValue.As<Napi::Object>();
        // validate the timebase object
        if (!timebaseObject.Has("timeBaseNum") || !timebaseObject.Get("timeBaseNum").IsNumber() ||
            !timebaseObject.Has("timeBaseDen") || !timebaseObject.Get("timeBaseDen").IsNumber())
        {
            Napi::TypeError::New(env, "invalid object for timeBase").ThrowAsJavaScriptException();
            return;
        }

        timeBases.push_back(timebaseObject);

        Napi::Value frameFrameValue = frameValue.As<Napi::Object>().Get("frame");
        if (!frameFrameValue.IsObject())
        {
            Napi::TypeError::New(env, "Object expected for frame").ThrowAsJavaScriptException();
            return;
        }
        AVFrameObject *frameObject = Napi::ObjectWrap<AVFrameObject>::Unwrap(frameFrameValue.As<Napi::Object>());
        if (!frameObject->frame_)
        {
            Napi::Error::New(env, "Invalid frame").ThrowAsJavaScriptException();
            return;
        }
        frames.push_back(frameObject->frame_);
    }

    if (!frames.size())
    {
        Napi::Error::New(env, "At least one frame is required").ThrowAsJavaScriptException();
        return;
    }

    bool isVideo = frames[0]->width || frames[0]->height;

    // outCount is the number of output frames
    Napi::Value outCountValue = options.Get("outCount");
    unsigned int outCount = 1;
    if (outCountValue.IsNumber())
    {
        outCount = outCountValue.As<Napi::Number>().Int32Value();
    }

    // threadCount
    Napi::Value threadCountValue = options.Get("threadCount");
    int threadCount = -1;
    if (threadCountValue.IsNumber())
    {
        threadCount = threadCountValue.As<Napi::Number>().Int32Value();
    }

    std::string filter_descr = filterValue.As<Napi::String>().Utf8Value();

    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = isVideo ? avfilter_get_by_name("buffer") : avfilter_get_by_name("abuffer");
    const AVFilter *buffersink = isVideo ? avfilter_get_by_name("buffersink") : avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = nullptr;
    AVFilterInOut *inputs = nullptr;

    struct AVFilterGraph *filter_graph = avfilter_graph_alloc();
    if (threadCount > 0)
        filter_graph->nb_threads = threadCount;
    if (!autoConvert)
        avfilter_graph_set_auto_convert(filter_graph, AVFILTER_AUTO_CONVERT_NONE);
    if (!filter_graph)
    {
        Napi::Error::New(env, "filter graph creation failed").ThrowAsJavaScriptException();
        goto end;
    }

    for (unsigned int i = 0; i < frames.size(); i++)
    {
        AVFrame *frame_ = frames[i];
        Napi::Object timeBase = timeBases[i];
        int timeBaseNum = timeBase.Get("timeBaseNum").As<Napi::Number>().Int32Value();
        int timeBaseDen = timeBase.Get("timeBaseDen").As<Napi::Number>().Int32Value();

        if (isVideo)
        {
            snprintf(args, sizeof(args),
                     "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                     frame_->width, frame_->height, frame_->format,
                     timeBaseNum, timeBaseDen,
                     1, 1);
        }
        else
        {
            snprintf(args, sizeof(args),
                     "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%llu",
                     timeBaseNum, timeBaseDen, frame_->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)frame_->format), frame_->ch_layout.u.mask);
        }

        char name[512];
        snprintf(name, sizeof(name), frames.size() == 1 ? "in" : "in%d", i);
        AVFilterContext *buffersrc_ctx;

        buffersrc_ctx = avfilter_graph_alloc_filter(filter_graph, buffersrc, name);
        if (!buffersrc_ctx)
        {
            Napi::Error::New(env, "Cannot alloc buffer source").ThrowAsJavaScriptException();
            goto end;
        }

        if (frame_->hw_frames_ctx)
        {
            AVBufferSrcParameters *src_params = av_buffersrc_parameters_alloc();
            if (!src_params)
            {
                Napi::Error::New(env, "Failed to allocate buffer source parameters").ThrowAsJavaScriptException();
                goto end;
            }
            src_params->hw_frames_ctx = av_buffer_ref(frame_->hw_frames_ctx);

            ret = av_buffersrc_parameters_set(buffersrc_ctx, src_params);
            av_freep(&src_params);

            if (ret < 0)
            {
                Napi::Error::New(env, "Failed to set buffer source parameters").ThrowAsJavaScriptException();
                goto end;
            }
        }

        ret = avfilter_init_str(buffersrc_ctx, args);
        if (ret < 0)
        {
            Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
            goto end;
        }

        buffersrc_ctxs.push_back(buffersrc_ctx);

        AVFilterInOut *output = avfilter_inout_alloc();
        if (!output)
        {
            Napi::Error::New(env, "Cannot allocate output").ThrowAsJavaScriptException();
            goto end;
        }
        output->name = av_strdup(name);
        output->filter_ctx = buffersrc_ctx;
        output->pad_idx = 0;
        output->next = NULL;

        if (!outputs)
        {
            outputs = output;
        }
        else
        {
            AVFilterInOut *lastOutput = outputs;
            while (lastOutput->next)
            {
                lastOutput = lastOutput->next;
            }
            lastOutput->next = output;
        }
    }

    for (unsigned int i = 0; i < outCount; i++)
    {
        char name[512];
        snprintf(name, sizeof(name), outCount == 1 ? "out" : "out%d", i);

        AVFilterContext *buffersink_ctx;
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, name,
                                           NULL, NULL, filter_graph);
        if (ret < 0)
        {
            Napi::Error::New(env, "Cannot create buffer sink").ThrowAsJavaScriptException();
            goto end;
        }

        buffersink_ctxs.push_back(buffersink_ctx);

        AVFilterInOut *input = avfilter_inout_alloc();
        if (!input)
        {
            Napi::Error::New(env, "Cannot allocate input").ThrowAsJavaScriptException();
            goto end;
        }
        input->name = av_strdup(name);
        input->filter_ctx = buffersink_ctx;
        input->pad_idx = 0;
        input->next = NULL;

        if (!inputs)
        {
            inputs = input;
        }
        else
        {
            AVFilterInOut *lastInput = inputs;
            while (lastInput->next)
            {
                lastInput = lastInput->next;
            }
            lastInput->next = input;
        }
    }

    if ((ret = avfilter_graph_parse_ptr2(filter_graph, filter_descr.c_str(),
                                         &inputs, &outputs, hw_device_ctx)) < 0)
    {
        Napi::Error::New(env, "Cannot parse filter graph").ThrowAsJavaScriptException();
        goto end;
    }

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
    {
        Napi::Error::New(env, "Cannot configure the filter graph").ThrowAsJavaScriptException();
        goto end;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    filterGraph = filter_graph;
    return;

end:
    avfilter_graph_free(&filter_graph);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
}

AVFilterGraphObject::~AVFilterGraphObject()
{
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

Napi::Value AVFilterGraphObject::SendCommand(const Napi::CallbackInfo &info)
{
    // need four arguments
    if (info.Length() < 3)
    {
        Napi::Error::New(info.Env(), "Expected 3 arguments to SendCommand").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    // parse out left, top, width, and height arguments as strings
    if (!info[0].IsString() || !info[1].IsString() || !info[2].IsString())
    {
        Napi::TypeError::New(info.Env(), "Strings expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // get the strings from the arguments
    std::string target = info[0].As<Napi::String>().Utf8Value();
    std::string command = info[1].As<Napi::String>().Utf8Value();
    std::string arg = info[2].As<Napi::String>().Utf8Value();
    int ret;

    // reset the x and y to zero so all widths and heights may be valid.
    if ((ret = avfilter_graph_send_command(filterGraph, target.c_str(), command.c_str(), arg.c_str(), 0, 0, 0)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Error while sending command to target %s %s %s\n", target.c_str(), command.c_str(), arg.c_str());
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}

Napi::Value AVFilterGraphObject::AddFrame(const Napi::CallbackInfo &info)
{
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(info.Env(), "Frame object expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    unsigned long index = 0;
    if (info.Length() > 1 && info[1].IsNumber())
    {
        index = info[1].As<Napi::Number>().Int32Value();
    }

    AVFrameObject *frameObject = Napi::ObjectWrap<AVFrameObject>::Unwrap(info[0].As<Napi::Object>());

    AVFrame *frame = frameObject->frame_;

    if (!frame)
    {
        Napi::TypeError::New(info.Env(), "Frame object is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (buffersrc_ctxs.size() <= index)
    {
        Napi::TypeError::New(info.Env(), "Buffersrc context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVFilterContext *buffersrc_ctx = buffersrc_ctxs[index];

    // Feed the frame to the buffer source filter
    int ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}

Napi::Value AVFilterGraphObject::GetFrame(const Napi::CallbackInfo &info)
{
    unsigned long index = 0;
    if (info.Length() && info[0].IsNumber())
    {
        index = info[0].As<Napi::Number>().Int32Value();
    }

    if (buffersink_ctxs.size() <= index)
    {
        Napi::TypeError::New(info.Env(), "Buffersink context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVFilterContext *buffersink_ctx = buffersink_ctxs[index];

    AVFrame *filtered_frame = av_frame_alloc();
    if (!filtered_frame)
    {
        Napi::Error::New(info.Env(), "Could not allocate filtered frame").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // Get the filtered frame from the buffer sink filter
    int ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame);
    // check for EAGAIN
    if (ret == AVERROR(EAGAIN))
    {
        av_frame_free(&filtered_frame);
        return info.Env().Undefined();
    }
    if (ret < 0)
    {
        av_frame_free(&filtered_frame);
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return AVFrameObject::NewInstance(info.Env(), filtered_frame);
}
