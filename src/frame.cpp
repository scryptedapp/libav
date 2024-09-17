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

#include "error.h"
#include "codeccontext.h"
#include "frame.h"
#include "filter.h"

Napi::FunctionReference AVFrameObject::constructor;

Napi::Object AVFrameObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVFrameObject", {

                                                                AVFrameObject::InstanceAccessor("width", &AVFrameObject::GetWidth, nullptr),

                                                                AVFrameObject::InstanceAccessor("height", &AVFrameObject::GetHeight, nullptr),

                                                                AVFrameObject::InstanceAccessor("pixelFormat", &AVFrameObject::GetPixelFormat, nullptr),

                                                                AVFrameObject::InstanceAccessor("timeBaseNum", &AVFrameObject::GetTimeBaseNum, nullptr),

                                                                AVFrameObject::InstanceAccessor("timeBaseDen", &AVFrameObject::GetTimeBaseDen, nullptr),

                                                                InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFrameObject::Destroy),

                                                                InstanceMethod("destroy", &AVFrameObject::Destroy),

                                                                InstanceMethod("toBuffer", &AVFrameObject::ToBuffer),

                                                                InstanceMethod("toJpeg", &AVFrameObject::ToJPEG),

                                                                InstanceMethod("createEncoder", &AVFrameObject::CreateEncoder),

                                                                InstanceMethod("createFilter", &AVFrameObject::CreateFilter),

                                                            });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    // exports.Set("AVFrameObject", func);
    return exports;
}

// Factory method to create an instance from C++
Napi::Object AVFrameObject::NewInstance(Napi::Env env, AVFrame *frame)
{
    Napi::Object obj = constructor.New({});
    AVFrameObject *wrapper = Napi::ObjectWrap<AVFrameObject>::Unwrap(obj);
    wrapper->frame_ = frame;
    return obj;
}

AVFrameObject::AVFrameObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFrameObject>(info), frame_(nullptr)
{
}

AVFrameObject::~AVFrameObject()
{
}

Napi::Value AVFrameObject::ToJPEG(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber())
    {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec)
    {
        Napi::Error::New(env, "Failed to find MJPEG codec").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c)
    {
        Napi::Error::New(env, "Failed to allocate codec context").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    c->bit_rate = 2000000; // This is generally less relevant for single images
    c->width = frame_->width;
    c->height = frame_->height;
    c->pix_fmt = (enum AVPixelFormat)frame_->format;
    c->time_base.num = 1;
    c->time_base.den = 25;

    // Set the JPEG quality
    int quality = info[0].As<Napi::Number>().Int32Value(); // 1-31, lower is better quality
    av_opt_set_int(c, "qscale", quality, 0);               // Set the quality scale (1-31, lower is better quality)
    av_opt_set_int(c, "qmin", quality, 0);                 // Set the minimum quality
    av_opt_set_int(c, "qmax", quality, 0);                 // Set the maximum quality

    int ret;
    if ((ret = avcodec_open2(c, codec, NULL)) < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if ((ret = avcodec_send_frame(c, frame_)) < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVPacket *pkt = av_packet_alloc();
    if ((ret = avcodec_receive_packet(c, pkt)) < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, pkt->data, pkt->size);
    av_packet_free(&pkt);
    avcodec_free_context(&c);

    return buffer;
}

Napi::Value AVFrameObject::CreateEncoder(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: options").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Object options = info[0].As<Napi::Object>();
    Napi::Value codecNameValue = options.Get("encoder");

    if (!codecNameValue.IsString())
    {
        Napi::TypeError::New(env, "String expected for encoder").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    const AVCodec *codec = avcodec_find_encoder_by_name(codecNameValue.As<Napi::String>().Utf8Value().c_str());

    if (!codec)
    {
        Napi::Error::New(env, "Failed to find encoder").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value bitrate = options.Get("bitrate");
    if (!bitrate.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for bitrate").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value timeBaseNum = options.Get("timeBaseNum");
    if (!timeBaseNum.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for timeBaseNum").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value timeBaseDen = options.Get("timeBaseDen");
    if (!timeBaseDen.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for timeBaseDen").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c)
    {
        Napi::Error::New(env, "Failed to allocate codec context").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    c->bit_rate = bitrate.As<Napi::Number>().Int32Value();
    c->width = frame_->width;
    c->height = frame_->height;
    c->pix_fmt = (enum AVPixelFormat)frame_->format;
    c->time_base.num = timeBaseNum.As<Napi::Number>().Int32Value();
    c->time_base.den = timeBaseDen.As<Napi::Number>().Int32Value();

    Napi::Value opts = options.Get("opts");
    if (opts.IsObject())
    {
        Napi::Object optsObj = opts.As<Napi::Object>();
        Napi::Array keys = optsObj.GetPropertyNames();
        for (size_t i = 0; i < keys.Length(); i++)
        {
            Napi::String key = keys.Get(i).As<Napi::String>();
            Napi::Value value = optsObj.Get(key);
            if (value.IsString())
            {
                av_opt_set(c->priv_data, key.Utf8Value().c_str(), value.As<Napi::String>().Utf8Value().c_str(), 0);
            }
            else if (value.IsNumber())
            {
                av_opt_set_int(c->priv_data, key.Utf8Value().c_str(), value.As<Napi::Number>().Int32Value(), 0);
            }
        }
    }

    if (frame_->hw_frames_ctx)
    {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(frame_->hw_frames_ctx->data);
        AVBufferRef *hw_device_ctx = frames_ctx->device_ref;
        c->hw_frames_ctx = av_buffer_ref(frame_->hw_frames_ctx);
        if (hw_device_ctx)
        {
            c->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        }
    }

    int ret;
    if ((ret = avcodec_open2(c, codec, NULL)) < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value codecContextObject = AVCodecContextObject::NewInstance(env);
    AVCodecContextObject *codecContextWrapper = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextObject.As<Napi::Object>());
    codecContextWrapper->codecContext = c;
    return codecContextObject;
}

Napi::Value AVFrameObject::CreateFilter(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: options").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // need filter, codecContext, timeBaseNum, timeBaseDen
    Napi::Object options = info[0].As<Napi::Object>();

    Napi::Value filterValue = options.Get("filter");
    if (!filterValue.IsString())
    {
        Napi::TypeError::New(env, "String expected for filter").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Value timeBaseNumValue = options.Get("timeBaseNum");
    if (!timeBaseNumValue.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for timeBaseNum").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Value timeBaseDenValue = options.Get("timeBaseDen");
    if (!timeBaseDenValue.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for timeBaseDen").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string filter_descr = filterValue.As<Napi::String>().Utf8Value();

    Napi::Value codecContextValue = options.Get("codecContext");
    AVCodecContextObject *codecContextObject = nullptr;

    if (codecContextValue.IsObject())
    {
        codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextValue.As<Napi::Object>());
    }

    enum AVPixelFormat sw_fmt = AV_PIX_FMT_YUVJ420P;
    enum AVPixelFormat pix_fmt = codecContextObject && codecContextObject->codecContext->hw_frames_ctx
                                     ? codecContextObject->hw_pix_fmt
                                     : sw_fmt;
    AVRational time_base = {timeBaseNumValue.As<Napi::Number>().Int32Value(), timeBaseDenValue.As<Napi::Number>().Int32Value()};

    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVBufferSrcParameters *src_params;
    AVFilterContext *buffersrc_ctx;
    AVFilterContext *buffersink_ctx;

    struct AVFilterGraph *filter_graph = avfilter_graph_alloc();
    avfilter_graph_set_auto_convert(filter_graph, AVFILTER_AUTO_CONVERT_NONE);
    if (!outputs || !inputs || !filter_graph)
    {
        Napi::Error::New(env, "filter graph creation failed").ThrowAsJavaScriptException();
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame_->width, frame_->height, pix_fmt,
             time_base.num, time_base.den,
             1, 1);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0)
    {
        Napi::Error::New(env, "Cannot create buffer source").ThrowAsJavaScriptException();
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, nullptr, filter_graph);
    if (ret < 0)
    {
        Napi::Error::New(env, "Cannot create buffer sink").ThrowAsJavaScriptException();
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_descr.c_str(),
                                        &inputs, &outputs, NULL)) < 0)
    {
        Napi::Error::New(env, "Cannot parse filter graph").ThrowAsJavaScriptException();
        goto end;
    }

    // 3. Set the hardware frames context for the buffer source
    src_params = av_buffersrc_parameters_alloc();
    if (!src_params)
    {
        Napi::Error::New(env, "Failed to allocate buffer source parameters").ThrowAsJavaScriptException();
        goto end;
    }

    // 2. Create a hardware frame context from the device context
    if (codecContextObject && codecContextObject->codecContext->hw_frames_ctx)
    {
        src_params->hw_frames_ctx = av_buffer_ref(codecContextObject->codecContext->hw_frames_ctx);
    }

    ret = av_buffersrc_parameters_set(buffersrc_ctx, src_params);
    av_freep(&src_params);

    if (ret < 0)
    {
        Napi::Error::New(env, "Failed to set buffer source parameters").ThrowAsJavaScriptException();
        goto end;
    }

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
    {
        Napi::Error::New(env, "Cannot configure the filter graph").ThrowAsJavaScriptException();
        goto end;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return AVFilterGraphObject::NewInstance(env, filter_graph, buffersrc_ctx, buffersink_ctx);

end:
    avfilter_graph_free(&filter_graph);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return env.Undefined();
}

Napi::Value AVFrameObject::ToBuffer(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!frame_->data[0])
    {
        Napi::Error::New(env, "Frame data is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Calculate the required buffer size
    int width = frame_->width;
    int height = frame_->height;
    int buffer_size = av_image_get_buffer_size((enum AVPixelFormat)frame_->format, width, height, 1);

    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, buffer_size);

    uint8_t *byte_array = buffer.Data();
    if (!byte_array)
    {
        Napi::Error::New(env, "Failed to allocate memory for byte array").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int frame_size = frame_->linesize[0] * height;
    if (frame_size != buffer_size)
    {
        int stride = buffer_size / height;
        for (int y = 0; y < height; y++)
        {
            memcpy(byte_array + y * stride, frame_->data[0] + y * frame_->linesize[0], stride);
        }
    }
    else
    {
        memcpy(byte_array, frame_->data[0], buffer_size);
    }

    return buffer;
}

Napi::Value AVFrameObject::Destroy(const Napi::CallbackInfo &info)
{
    if (frame_)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    return info.Env().Undefined();
}

Napi::Value AVFrameObject::GetWidth(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->width);
}

Napi::Value AVFrameObject::GetHeight(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->height);
}

Napi::Value AVFrameObject::GetPixelFormat(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    enum AVPixelFormat pix_fmt = (enum AVPixelFormat)frame_->format;
    const char *format = av_get_pix_fmt_name(pix_fmt);
    return Napi::String::New(env, format);
}

Napi::Value AVFrameObject::GetTimeBaseNum(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->time_base.num);
}

Napi::Value AVFrameObject::GetTimeBaseDen(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->time_base.den);
}
