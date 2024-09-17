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
// rtpenc.h header uses a weird restrict keyword
#ifndef restrict
#define restrict
#include <libavformat/rtpenc.h>
#undef restrict
#else
#include <libavformat/rtpenc.h>
#endif
}

#include <thread>
#include <v8.h>

#include "filter.h"
#include "codeccontext.h"
#include "packet.h"

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
    Napi::ThreadSafeFunction callbackRef;

private:
    Napi::Value GetTimeBaseNum(const Napi::CallbackInfo &info);
    Napi::Value GetTimeBaseDen(const Napi::CallbackInfo &info);
    Napi::Value Open(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value CreateDecoder(const Napi::CallbackInfo &info);
    Napi::Value GetMetadata(const Napi::CallbackInfo &info);
    Napi::Value ReadFrame(const Napi::CallbackInfo &info);
    Napi::Value Create(const Napi::CallbackInfo &info);
    Napi::Value NewStream(const Napi::CallbackInfo &info);
    Napi::Value WriteFrame(const Napi::CallbackInfo &info);
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

                                                                  AVFormatContextObject::InstanceAccessor("metadata", &AVFormatContextObject::GetMetadata, nullptr),

                                                                  AVFormatContextObject::InstanceAccessor("timeBaseNum", &AVFormatContextObject::GetTimeBaseNum, nullptr),

                                                                  AVFormatContextObject::InstanceAccessor("timeBaseDen", &AVFormatContextObject::GetTimeBaseDen, nullptr),

                                                                  InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFormatContextObject::Close),

                                                                  InstanceMethod("open", &AVFormatContextObject::Open),

                                                                  InstanceMethod("close", &AVFormatContextObject::Close),

                                                                  InstanceMethod("createDecoder", &AVFormatContextObject::CreateDecoder),

                                                                  InstanceMethod("readFrame", &AVFormatContextObject::ReadFrame),

                                                                  InstanceMethod("create", &AVFormatContextObject::Create),

                                                                  InstanceMethod("newStream", &AVFormatContextObject::NewStream),

                                                                  InstanceMethod("writeFrame", &AVFormatContextObject::WriteFrame),
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

Napi::Value AVFormatContextObject::GetTimeBaseNum(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!fmt_ctx_)
    {
        return env.Undefined();
    }
    AVRational time_base = fmt_ctx_->streams[videoStreamIndex]->time_base;
    return Napi::Number::New(env, time_base.num);
}

Napi::Value AVFormatContextObject::GetTimeBaseDen(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!fmt_ctx_)
    {
        return env.Undefined();
    }
    AVRational time_base = fmt_ctx_->streams[videoStreamIndex]->time_base;
    return Napi::Number::New(env, time_base.den);
}

// Custom write function to intercept RTP packet data
static int write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    AVFormatContextObject *formatContextObject = (AVFormatContextObject *)opaque;
    // fprintf(stderr, "write_packet called with size: %d\n", buf_size);

    // Call the JavaScript function on the main thread
    formatContextObject->callbackRef.BlockingCall([buf, buf_size](Napi::Env env, Napi::Function jsCallback)
                                                  {
                                                      Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, buf, buf_size);
                                                      jsCallback.Call({buffer});
                                                      //
                                                  });

    return buf_size;
}

// Custom write funct
Napi::Value AVFormatContextObject::Create(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (fmt_ctx_)
    {
        Napi::Error::New(env, "Format context already created").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsString())
    {
        Napi::TypeError::New(env, "String expected for argument 0: format").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 2 || !info[1].IsFunction())
    {
        Napi::TypeError::New(env, "Function expected for argument 1: callback").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string formatName = info[0].As<Napi::String>().Utf8Value();

    int ret = avformat_alloc_output_context2(&fmt_ctx_, NULL, formatName.c_str(), NULL);
    if (ret)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // RTP doesn't initialize properly?
    if (formatName == "rtp")
    {
        int MAX_RTP_PACKET_SIZE = 65536;
        RTPMuxContext *rtp_ctx = (RTPMuxContext *)fmt_ctx_->priv_data;
        rtp_ctx->max_payload_size = MAX_RTP_PACKET_SIZE - 12;
        rtp_ctx->buf = (uint8_t *)av_malloc(MAX_RTP_PACKET_SIZE);
        if (!rtp_ctx->buf)
        {
            avformat_free_context(fmt_ctx_);
            fmt_ctx_ = nullptr;
            Napi::Error::New(env, "Failed to allocate RTP buffer").ThrowAsJavaScriptException();
            return env.Undefined();
        }
    }

    // Set up a custom AVIOContext to capture the RTP output
    int MAX_MEM_SIZE = 1024 * 1024;
    uint8_t *buffer = (uint8_t *)av_malloc(MAX_MEM_SIZE);
    if (!buffer)
    {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
        Napi::Error::New(env, "Failed to allocate AVIO context buffer").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVIOContext *avio_ctx = avio_alloc_context(buffer, MAX_MEM_SIZE, 1, this, NULL, write_packet, NULL);
    if (!avio_ctx)
    {
        av_free(buffer);
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
        Napi::Error::New(env, "Failed to create AVIOContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    callbackRef = Napi::ThreadSafeFunction::New(
        env,                          // Environment
        info[1].As<Napi::Function>(), // JavaScript function to call
        "napi_write_packet",          // Resource name for diagnostics
        0,                            // Max queue size (0 = unlimited)
        1                             // Initial thread count
    );

    fmt_ctx_->pb = avio_ctx;

    return env.Undefined();
}

Napi::Value AVFormatContextObject::WriteFrame(const Napi::CallbackInfo &info)
{
    // arguments are index and packet
    Napi::Env env = info.Env();

    if (!fmt_ctx_)
    {
        Napi::Error::New(env, "Format context is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsObject())
    {
        Napi::TypeError::New(env, "Number expected for argument 0: index and Object expected for argument 1: packet").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int streamIndex = info[0].As<Napi::Number>().Int32Value();

    Napi::Object packetObject = info[1].As<Napi::Object>();
    AVPacketObject *avPacketObject = Napi::ObjectWrap<AVPacketObject>::Unwrap(packetObject);
    AVPacket *packet = avPacketObject->packet;

    if (!packet)
    {
        Napi::Error::New(env, "Packet is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    packet->stream_index = streamIndex;

    av_write_frame(fmt_ctx_, packet);

    return env.Undefined();
}

Napi::Value AVFormatContextObject::NewStream(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!fmt_ctx_)
    {
        Napi::Error::New(env, "Format context is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: codecContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Object options = info[0].As<Napi::Object>();

    Napi::Value codecContextValue = options.Get("codecContext");
    if (!codecContextValue.IsObject())
    {
        Napi::TypeError::New(env, "Object expected for codecContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AVCodecContextObject *codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextValue.As<Napi::Object>());
    AVCodecContext *codecContext = codecContextObject->codecContext;

    // Create a new stream
    AVStream *stream = avformat_new_stream(fmt_ctx_, NULL);
    if (!stream)
    {
        Napi::Error::New(env, "Failed to create new stream").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Set the codec parameters for the new stream
    AVCodecParameters *codecpar = stream->codecpar;
    stream->time_base = (AVRational){codecContext->time_base.num, codecContext->time_base.den}; // Set the time base to 25 fps

    avcodec_parameters_from_context(codecpar, codecContext);

    // gpt says supposed to send this but sends empty packet which crashes.
    // sps/pps seem to be sent correctly though.
    // may be an issue on mp4 where there's a moov header?
    // int ret;
    // if ((ret = avformat_write_header(fmt_ctx_, NULL)) < 0)
    // {
    //     avformat_free_context(fmt_ctx_);
    //     fmt_ctx_ = nullptr;
    //     Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
    //     return env.Undefined();
    // }

    return Napi::Number::New(env, stream->index);
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();

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