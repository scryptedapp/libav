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

#include "filter.cpp"
#include "codeccontext.cpp"

static Napi::FunctionReference logCallbackRef;

// Custom log callback to pass messages to JavaScript
static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    v8::Isolate *isolate = v8::Isolate::GetCurrent();
    if (!isolate)
    {
        return;
    }

    // Create a formatted string
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, vl);

    // Retrieve the environment and callback function
    Napi::Env env = logCallbackRef.Env();
    Napi::Function callback = logCallbackRef.Value();

    callback.Call(env.Global(), {Napi::String::New(env, buffer), Napi::Number::New(env, level)});
}

Napi::Value setLogCallback(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() < 1)
    {
        logCallbackRef.Reset();
        return env.Undefined();
    }

    logCallbackRef = Napi::Persistent(info[0].As<Napi::Function>()); // Save the log callback function

    av_log_set_callback(ffmpeg_log_callback);
    return env.Undefined();
}

Napi::Value setLogLevel(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // ensure 1 param
    if (info.Length() < 1)
    {
        Napi::Error::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    // get string from info
    std::string level = info[0].As<Napi::String>().Utf8Value();
    if (level == "debug")
    {
        av_log_set_level(AV_LOG_DEBUG);
    }
    else if (level == "verbose")
    {
        av_log_set_level(AV_LOG_VERBOSE);
    }
    else if (level == "info")
    {
        av_log_set_level(AV_LOG_INFO);
    }
    else if (level == "warning")
    {
        av_log_set_level(AV_LOG_WARNING);
    }
    else if (level == "error")
    {
        av_log_set_level(AV_LOG_ERROR);
    }
    else if (level == "fatal")
    {
        av_log_set_level(AV_LOG_FATAL);
    }
    else if (level == "panic")
    {
        av_log_set_level(AV_LOG_PANIC);
    }
    else if (level == "quiet")
    {
        av_log_set_level(AV_LOG_QUIET);
    }
    else if (level == "trace")
    {
        av_log_set_level(AV_LOG_TRACE);
    }
    else
    {
        Napi::Error::New(env, "Invalid log level").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return env.Undefined();
}

void printAVError(int errnum)
{
    char errbuf[256];

    // Convert the error code to a string
    if (av_strerror(errnum, errbuf, sizeof(errbuf)) < 0)
    {
        printf("Unknown error code: %d", errnum);
        return;
    }

    printf("Error: %s\n", errbuf);
}

class AVFormatContextObject : public Napi::ObjectWrap<AVFormatContextObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFormatContextObject(const Napi::CallbackInfo &info);
    ~AVFormatContextObject(); // Explicitly declare the destructor
    static Napi::FunctionReference constructor;
    AVFormatContext *fmt_ctx_;
    int videoStreamIndex;

private:
    Napi::Value Open(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value CreateDecoder(const Napi::CallbackInfo &info);
    Napi::Value GetMetadata(const Napi::CallbackInfo &info);
    Napi::Value ReadFrame(const Napi::CallbackInfo &info);
    Napi::Value CreateFilter(const Napi::CallbackInfo &info);
};

class ReadFrameWorker : public Napi::AsyncWorker
{
public:
    ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject)
        : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject)
    {
    }

    void Execute() override
    {
        AVFormatContext *fmt_ctx_ = formatContextObject->fmt_ctx_;
        if (!fmt_ctx_)
        {
            SetError("Format context is null");
            return;
        }

        AVPacket *packet = av_packet_alloc();
        int ret;

        if (!packet)
        {
            SetError("Failed to allocate packet");
            return;
        }

        while (true)
        {
            ret = av_read_frame(fmt_ctx_, packet);
            if (ret == AVERROR(EAGAIN))
            {
                // try reading again later
                av_packet_free(&packet);
                result = nullptr;
                return;
            }
            else if (ret)
            {
                av_packet_free(&packet);
                SetError(AVErrorString(ret));
                return;
            }

            if (packet->stream_index == formatContextObject->videoStreamIndex)
            {
                result = packet;
                break;
            }
            // not video so keep looping.
            av_packet_unref(packet);
        }
    }

    // This method runs in the main thread after Execute completes successfully
    void OnOK() override
    {
        Napi::Env env = Env();
        if (!result)
        {
            napi_resolve_deferred(Env(), deferred, env.Undefined());
        }
        else
        {
            napi_resolve_deferred(Env(), deferred, AVPacketObject::NewInstance(env, result));
        }
    }

    // This method runs in the main thread if Execute fails
    void OnError(const Napi::Error &e) override
    {
        napi_value error = e.Value();

        // Reject the promise
        napi_reject_deferred(Env(), deferred, error);
    }

private:
    AVPacket *result;
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
};

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    AVCodecContextObject *fmt = (AVCodecContextObject *)ctx->opaque;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (*p == fmt->hw_pix_fmt)
            return *p;
    }

    // fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

Napi::FunctionReference AVFormatContextObject::constructor;

Napi::Object AVFormatContextObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVFormatContext", {
                                                                  InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFormatContextObject::Close),

                                                                  InstanceMethod("open", &AVFormatContextObject::Open),

                                                                  InstanceMethod("close", &AVFormatContextObject::Close),

                                                                  InstanceMethod("createDecoder", &AVFormatContextObject::CreateDecoder),

                                                                  InstanceMethod("readFrame", &AVFormatContextObject::ReadFrame),

                                                                  InstanceMethod("createFilter", &AVFormatContextObject::CreateFilter),

                                                                  AVFormatContextObject::InstanceAccessor("metadata", &AVFormatContextObject::GetMetadata, nullptr),
                                                              });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVFormatContext", func);
    return exports;
}

AVFormatContextObject::AVFormatContextObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFormatContextObject>(info),
      fmt_ctx_(nullptr),
      videoStreamIndex(-1)
{
    // i don't think this constructor is called from js??
}

Napi::Value AVFormatContextObject::ReadFrame(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    ReadFrameWorker *worker = new ReadFrameWorker(env, deferred, this);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVFormatContextObject::CreateFilter(const Napi::CallbackInfo &info)
{
    if (!fmt_ctx_)
    {
        Napi::Error::New(info.Env(), "Format context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    Napi::Env env = info.Env();
    if (info.Length() < 4)
    {
        Napi::Error::New(env, "Expected 4 arguments to FilterFrame").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // outWidth: number, outHeight: number, filter: string, codecContext (with hardware frames context)
    if (!info[0].IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for argument 0: frameWidth").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[1].IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for argument 1: frameHeight").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[2].IsString())
    {
        Napi::TypeError::New(env, "String expected for argument 2: filter").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[3].IsObject())
    {
        Napi::TypeError::New(env, "AVCodecContext expected for argument 3: codecContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int width = info[0].As<Napi::Number>().Int32Value();
    int height = info[1].As<Napi::Number>().Int32Value();
    std::string filter_descr = info[2].As<Napi::String>().Utf8Value();

    AVCodecContextObject *codecContextObject = nullptr;
    if (info[3].IsObject())
    {
        codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(info[3].As<Napi::Object>());
    }

    enum AVPixelFormat sw_fmt = AV_PIX_FMT_YUVJ420P;
    enum AVPixelFormat pix_fmt = codecContextObject && codecContextObject->codecContext->hw_frames_ctx
                                     ? codecContextObject->hw_pix_fmt
                                     : sw_fmt;
    AVRational time_base = fmt_ctx_->streams[videoStreamIndex]->time_base;

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
             width, height, pix_fmt,
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

Napi::Value AVFormatContextObject::Open(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString())
    {
        Napi::TypeError::New(env, "String source expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVDictionary *options = nullptr;
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    int ret = avformat_open_input(&fmt_ctx_, filename.c_str(), NULL, &options);
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    const struct AVCodec *codec = nullptr;

    /* find the video stream information */
    ret = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ret < 0)
    {
        Napi::Error::New(env, "Cannot find a video stream in the input file").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    videoStreamIndex = ret;

    if (videoStreamIndex == -1 || codec == nullptr)
    {
        Napi::Error::New(env, "Failed to find a video stream or codec").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return env.Undefined();
}

Napi::Value AVFormatContextObject::CreateDecoder(const Napi::CallbackInfo &info)
{
    if (!fmt_ctx_)
    {
        Napi::Error::New(info.Env(), "Format context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    Napi::Env env = info.Env();

    AVCodecID codec_id = fmt_ctx_->streams[videoStreamIndex]->codecpar->codec_id;
    const struct AVCodec *codec = avcodec_find_decoder(codec_id);
    Napi::Object codecContextReturn = AVCodecContextObject::NewInstance(env);
    AVCodecContextObject *codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextReturn);
    codecContextObject->hw_device_value = AV_HWDEVICE_TYPE_NONE;
    AVBufferRef *hw_device_ctx = nullptr;
    int ret;

    // args are hardwareDeviceName (optional) hardwareDeviceDecoder (optional)
    if (info.Length())
    {
        if (!info[0].IsString())
        {
            Napi::Error::New(env, "Unexpected type in place of hardware device name string").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string hardwareDeviceName = info[0].As<Napi::String>().Utf8Value();
        codecContextObject->hw_device_value = av_hwdevice_find_type_by_name(hardwareDeviceName.c_str());
        if (codecContextObject->hw_device_value == AV_HWDEVICE_TYPE_NONE)
        {
            Napi::Error::New(env, "Hardware device not found").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // qsv path
        if (info.Length() > 1)
        {
            if (!info[1].IsString())
            {
                Napi::Error::New(env, "Unexpected type in place of decoder string").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            codec = avcodec_find_decoder_by_name(info[1].As<Napi::String>().Utf8Value().c_str());
            if (!codec)
            {
                Napi::Error::New(env, "Decoder not found").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }

        for (int i = 0;; i++)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
            if (!config)
            {
                Napi::Error::New(env, "Decoder does not support device type").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == codecContextObject->hw_device_value)
            {
                // unsure if this works for qsv?
                codecContextObject->hw_pix_fmt = config->pix_fmt;
                break;
            }
        }

        if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, codecContextObject->hw_device_value,
                                          NULL, NULL, 0)) < 0)
        {
            // throw
            Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
            return env.Undefined();
        }
    }

    codecContextObject->codecContext = avcodec_alloc_context3(codec);
    codecContextObject->codecContext->opaque = codecContextObject;
    if (hw_device_ctx)
    {
        codecContextObject->codecContext->get_format = get_hw_format;
        codecContextObject->codecContext->hw_device_ctx = hw_device_ctx;
    }

    if ((ret = avcodec_parameters_to_context(codecContextObject->codecContext, fmt_ctx_->streams[videoStreamIndex]->codecpar)) < 0)
    {
        avcodec_free_context(&codecContextObject->codecContext);
        codecContextObject->codecContext = nullptr;
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if ((ret = avcodec_open2(codecContextObject->codecContext, codec, nullptr)) < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        avcodec_free_context(&codecContextObject->codecContext);
        codecContextObject->codecContext = nullptr;
        return env.Undefined();
    }

    return codecContextReturn;
}

AVFormatContextObject::~AVFormatContextObject()
{
}

Napi::Value AVFormatContextObject::Close(const Napi::CallbackInfo &info)
{
    if (fmt_ctx_)
    {
        av_log(NULL, AV_LOG_INFO, "Closing AVFormatContext\n");
        avformat_close_input(&fmt_ctx_);
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }

    return info.Env().Undefined();
}

Napi::Value AVFormatContextObject::GetMetadata(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Object metadata = Napi::Object::New(env);

    if (fmt_ctx_ && fmt_ctx_->metadata)
    {
        AVDictionaryEntry *tag = NULL;
        while ((tag = av_dict_get(fmt_ctx_->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        {
            metadata.Set(tag->key, tag->value);
        }
    }

    return metadata;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    av_log_set_level(AV_LOG_QUIET);

    AVPacketObject::Init(env, exports);
    AVFilterGraphObject::Init(env, exports);
    AVFrameObject::Init(env, exports);
    AVCodecContextObject::Init(env, exports);
    AVFormatContextObject::Init(env, exports);

    exports.Set(Napi::String::New(env, "setLogCallback"), Napi::Function::New(env, setLogCallback));
    exports.Set(Napi::String::New(env, "setLogLevel"), Napi::Function::New(env, setLogLevel));
    return exports;
}

NODE_API_MODULE(addon, Init)