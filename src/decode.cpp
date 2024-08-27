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

static Napi::FunctionReference logCallbackRef;

// Custom log callback to pass messages to JavaScript
static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    v8::Isolate *isolate = v8::Isolate::GetCurrent();
    if (!isolate)
    {
        return;
    }

    if (level <= AV_LOG_DEBUG)
    {
        // Create a formatted string
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, vl);

        // Retrieve the environment and callback function
        Napi::Env env = logCallbackRef.Env();
        Napi::Function callback = logCallbackRef.Value();

        callback.Call(env.Global(), {Napi::String::New(env, buffer)});
    }
}

Napi::Value set_console_callback(const Napi::CallbackInfo &info)
{
    logCallbackRef = Napi::Persistent(info[0].As<Napi::Function>()); // Save the log callback function

    av_log_set_callback(ffmpeg_log_callback);
    av_log_set_level(AV_LOG_VERBOSE);
    Napi::Env env = info.Env();
    return env.Null();
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

class ReadFrameWorker : public Napi::AsyncWorker
{
public:
    ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContext *fmt_ctx, AVCodecContext *codecContext, int videoStreamIndex)
        : Napi::AsyncWorker(env), deferred(deferred), fmt_ctx_(fmt_ctx), codecContext(codecContext), videoStreamIndex(videoStreamIndex)
    {
    }

    void Execute() override
    {
        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            SetError("Failed to allocate packet");
            return;
        }

        AVFrame *frame = av_frame_alloc();
        if (!frame)
        {
            av_packet_free(&packet);
            SetError("Failed to allocate frame");
            return;
        }

        int ret;
        while (true)
        {
            // attempt to read frames first, EAGAIN will be returned if data needs to be
            // sent to decoder.
            ret = avcodec_receive_frame(codecContext, frame);
            if (!ret)
            {
                // printf("returning frame 0\n");
                av_packet_free(&packet);
                result = frame;
                return;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                printAVError(ret);
                av_packet_free(&packet);
                SetError("error during avcodec_receive_frame");
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
                    printAVError(ret);
                    av_packet_free(&packet);
                    SetError("Could not read frame");
                    return;
                }

                if (packet->stream_index == videoStreamIndex)
                {
                    break;
                }
                // not video so keep looping.
                av_packet_unref(packet);
            }

            ret = avcodec_send_packet(codecContext, packet);
            av_packet_unref(packet); // Reset the packet for the next frame

            // on decoder feed error, try again next packet.
            // could be starting on a non keyframe or data corruption
            // which may be recoverable.
            if (ret)
            {
                av_packet_free(&packet);
                fprintf(stderr, "Error sending packet to decoder.\n");
                printAVError(ret);
                result = nullptr;
                return;
            }
            // successfully send data to decoder, so try reading from it again immediately.
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
            napi_resolve_deferred(Env(), deferred, AVFrameObject::NewInstance(env, result));
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
    AVFrame *result;
    napi_deferred deferred;
    AVFormatContext *fmt_ctx_;
    AVCodecContext *codecContext;
    int videoStreamIndex;
};

class AVFormatContextObject : public Napi::ObjectWrap<AVFormatContextObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFormatContextObject(const Napi::CallbackInfo &info);
    ~AVFormatContextObject(); // Explicitly declare the destructor
    enum AVPixelFormat hw_pix_fmt;

private:
    static Napi::FunctionReference constructor;
    AVFormatContext *fmt_ctx_;
    int videoStreamIndex;
    AVCodecContext *codecContext;
    AVBufferRef *hw_device_ctx;
    AVHWFramesContext *frames_ctx;

    Napi::Value Open(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value GetMetadata(const Napi::CallbackInfo &info);
    Napi::Value GetPointer(const Napi::CallbackInfo &info);
    Napi::Value ReadFrame(const Napi::CallbackInfo &info);
    Napi::Value CreateFilter(const Napi::CallbackInfo &info);
};

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    AVFormatContextObject *fmt = (AVFormatContextObject *)ctx->opaque;

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
                                                                  InstanceMethod("open", &AVFormatContextObject::Open),

                                                                  InstanceMethod("close", &AVFormatContextObject::Close),

                                                                  InstanceMethod("getMetadata", &AVFormatContextObject::GetMetadata),

                                                                  InstanceMethod("getPointer", &AVFormatContextObject::GetPointer),

                                                                  InstanceMethod("readFrame", &AVFormatContextObject::ReadFrame),

                                                                  InstanceMethod("createFilter", &AVFormatContextObject::CreateFilter),
                                                              });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVFormatContext", func);
    return exports;
}

AVFormatContextObject::AVFormatContextObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFormatContextObject>(info),
      videoStreamIndex(-1),
      codecContext(nullptr),
      hw_device_ctx(nullptr)
{
    Napi::Env env = info.Env();

    if (info.Length() > 0 && info[0].IsBigInt())
    {
        // If a pointer is provided, use it
        Napi::BigInt bigint = info[0].As<Napi::BigInt>();
        bool lossless;
        uint64_t value = bigint.Uint64Value(&lossless);

        if (!lossless)
        {
            Napi::Error::New(env, "BigInt conversion was not lossless").ThrowAsJavaScriptException();
            return;
        }

        fmt_ctx_ = reinterpret_cast<AVFormatContext *>(value);
    }
    else
    {
        // If no pointer is provided, allocate a new context
        fmt_ctx_ = avformat_alloc_context();
        if (!fmt_ctx_)
        {
            Napi::Error::New(env, "Failed to allocate AVFormatContext").ThrowAsJavaScriptException();
            return;
        }
    }
}

Napi::Value AVFormatContextObject::ReadFrame(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    ReadFrameWorker *worker = new ReadFrameWorker(env, deferred, fmt_ctx_, codecContext, videoStreamIndex);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVFormatContextObject::CreateFilter(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() < 4)
    {
        Napi::Error::New(env, "Expected 4 arguments to FilterFrame").ThrowAsJavaScriptException();
        return env.Null();
    }

    // outWidth: number, outHeight: number, filter: string, use_hw: boolean
    if (!info[0].IsNumber())
    {
        Napi::TypeError::New(env, "Object expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[1].IsNumber())
    {
        Napi::TypeError::New(env, "Object expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[2].IsString())
    {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[3].IsBoolean())
    {
        Napi::TypeError::New(env, "Boolean expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    int width = info[0].As<Napi::Number>().Int32Value();
    int height = info[1].As<Napi::Number>().Int32Value();
    std::string filter_descr = info[2].As<Napi::String>().Utf8Value();
    bool use_hw = info[3].As<Napi::Boolean>().Value();

    enum AVPixelFormat sw_fmt = AV_PIX_FMT_YUVJ420P;
    enum AVPixelFormat pix_fmt = use_hw ? hw_pix_fmt : sw_fmt;
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
    AVBufferRef *hw_frames_ctx;

    struct AVFilterGraph *filter_graph = avfilter_graph_alloc();
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
    if (hw_device_ctx)
    {
        hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!hw_frames_ctx)
        {
            Napi::Error::New(env, "Failed to allocate hardware frame context").ThrowAsJavaScriptException();
            goto end;
        }

        frames_ctx = (AVHWFramesContext *)hw_frames_ctx->data;
        frames_ctx->format = hw_pix_fmt;
        frames_ctx->sw_format = AV_PIX_FMT_NV12; // or any other software format
        frames_ctx->width = width;
        frames_ctx->height = height;
        frames_ctx->initial_pool_size = 20;

        ret = av_hwframe_ctx_init(hw_frames_ctx);
        if (ret < 0)
        {
            av_buffer_unref(&hw_frames_ctx);
            Napi::Error::New(env, "Failed to initialize hardware frame context").ThrowAsJavaScriptException();
            goto end;
        }

        src_params->hw_frames_ctx = hw_frames_ctx;
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

    return env.Null();
}

Napi::Value AVFormatContextObject::Open(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString())
    {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string decoder;
    if (info.Length() > 1 && info[1].IsString())
    {
        decoder = info[1].As<Napi::String>().Utf8Value();
    }

    AVDictionary *options = nullptr;
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    int ret = avformat_open_input(&fmt_ctx_, filename.c_str(), NULL, &options);
    if (ret < 0)
    {
        Napi::Error::New(env, "Could not open input").ThrowAsJavaScriptException();
        return env.Null();
    }

// Find the first video stream
#ifdef __APPLE__
    const struct AVCodec *codec = nullptr;
#else
    struct AVCodec *codec = nullptr;
#endif

    /* find the video stream information */
    ret = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ret < 0)
    {
        Napi::Error::New(env, "Cannot find a video stream in the input file").ThrowAsJavaScriptException();
        return env.Null();
    }
    videoStreamIndex = ret;

    if (videoStreamIndex == -1 || codec == nullptr)
    {
        Napi::Error::New(env, "Failed to find a video stream or codec").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Open codec
    codecContext = avcodec_alloc_context3(codec);
    codecContext->opaque = this;
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        Napi::Error::New(env, "Failed to open codec").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (decoder.length())
    {
        enum AVHWDeviceType hw_device_value = av_hwdevice_find_type_by_name(decoder.c_str());
        if (hw_device_value == AV_HWDEVICE_TYPE_NONE)
        {
            Napi::Error::New(env, "Invalid decoder").ThrowAsJavaScriptException();
            return env.Null();
        }

        for (int i = 0;; i++)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
            if (!config)
            {
                Napi::Error::New(env, "Decoder does not support device type").ThrowAsJavaScriptException();
                return Napi::String::New(env, "Decoder %s does not support device type");
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == hw_device_value)
            {
                hw_pix_fmt = config->pix_fmt;
                break;
            }
        }

        if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, hw_device_value,
                                          NULL, NULL, 0)) < 0)
        {
            Napi::Error::New(env, "Failed to create specified HW device").ThrowAsJavaScriptException();
            return env.Null();
        }

        codecContext->get_format = get_hw_format;
        codecContext->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }

    return Napi::Boolean::New(env, true);
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

    if (fmt_ctx_->metadata)
    {
        AVDictionaryEntry *tag = NULL;
        while ((tag = av_dict_get(fmt_ctx_->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        {
            metadata.Set(tag->key, tag->value);
        }
    }

    return metadata;
}

Napi::Value AVFormatContextObject::GetPointer(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    return Napi::BigInt::New(env, reinterpret_cast<uint64_t>(fmt_ctx_));
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    // int error_code = AVERROR(EAGAIN);
    // printf("AVERROR(EAGAIN) = %d\n", error_code);
    // error_code = AVERROR(AVERROR_EOF);
    // printf("AVERROR(AVERROR_EOF) = %d\n", error_code);
    // error_code = AVERROR(EINVAL);
    // printf("AVERROR(EINVAL) = %d\n", error_code);

    AVFilterGraphObject::Init(env, exports);
    AVFrameObject::Init(env, exports);
    AVFormatContextObject::Init(env, exports);

    exports.Set(Napi::String::New(env, "set_console_callback"), Napi::Function::New(env, set_console_callback));
    return exports;
}

NODE_API_MODULE(addon, Init)