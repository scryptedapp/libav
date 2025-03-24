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
#include <libavutil/avutil.h>
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

#include "formatcontext.h"
#include "filter.h"
#include "codeccontext.h"
#include "packet.h"
#include "bsf.h"
#include "read-frame-worker.h"

static Napi::FunctionReference logCallbackRef;

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

                                                                  AVFormatContextObject::InstanceAccessor("streams", &AVFormatContextObject::GetStreams, nullptr),

                                                                  InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFormatContextObject::Close),

                                                                  InstanceMethod("open", &AVFormatContextObject::Open),

                                                                  InstanceMethod("close", &AVFormatContextObject::Close),

                                                                  InstanceMethod("createDecoder", &AVFormatContextObject::CreateDecoder),

                                                                  InstanceMethod("readFrame", &AVFormatContextObject::ReadFrame),

                                                                  InstanceMethod("receiveFrame", &AVFormatContextObject::ReceiveFrame),

                                                                  InstanceMethod("create", &AVFormatContextObject::Create),

                                                                  InstanceMethod("newStream", &AVFormatContextObject::NewStream),

                                                                  InstanceMethod("writeFrame", &AVFormatContextObject::WriteFrame),

                                                                  InstanceMethod("createSDP", &AVFormatContextObject::CreateSDP),
                                                              });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVFormatContext", func);
    return exports;
}

AVFormatContextObject::AVFormatContextObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVFormatContextObject>(info),
      fmt_ctx_(nullptr), is_input(false)
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
    ReadFrameWorker *worker = new ReadFrameWorker(env, deferred, this, nullptr, 0);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVFormatContextObject::ReceiveFrame(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    if (info.Length() < 1 || !info[0].IsNumber())
    {
        Napi::TypeError::New(env, "Number expected for argument 1: streamIndex").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    int streamIndex = info[0].As<Napi::Number>().Int32Value();

    if (info.Length() < 2 || !info[1].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: codecContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    AVCodecContextObject *codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(info[1].As<Napi::Object>());

    // Create and queue the AsyncWorker, passing the deferred handle
    ReadFrameWorker *worker = new ReadFrameWorker(env, deferred, this, codecContextObject, streamIndex);
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

    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    std::string filename = info[0].As<Napi::String>().Utf8Value();

    // Create and queue the AsyncWorker, passing the deferred handle
    OpenWorker *worker = new OpenWorker(env, deferred, this, filename);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVFormatContextObject::CreateDecoder(const Napi::CallbackInfo &info)
{
    if (!fmt_ctx_)
    {
        Napi::Error::New(info.Env(), "Format context is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    if (info.Length() < 1 || !info[0].IsNumber())
    {
        Napi::TypeError::New(info.Env(), "Number expected for argument 0: streamIndex").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
    int streamIndex = info[0].As<Napi::Number>().Int32Value();

    Napi::Env env = info.Env();

    AVStream *stream = fmt_ctx_->streams[streamIndex];
    AVCodecID codec_id = stream->codecpar->codec_id;
    const struct AVCodec *codec = avcodec_find_decoder(codec_id);
    Napi::Object codecContextReturn = AVCodecContextObject::NewInstance(env);
    AVCodecContextObject *codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextReturn);
    codecContextObject->hw_device_value = AV_HWDEVICE_TYPE_NONE;
    AVBufferRef *hw_device_ctx = nullptr;
    int ret;
    std::string deviceName;

    // args are hardwareDeviceName (optional) hardwareDeviceDecoder (optional)
    if (info.Length() > 1)
    {
        if (!info[1].IsString())
        {
            Napi::Error::New(env, "Unexpected type in place of hardware device name string").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string hardwareDeviceName = info[1].As<Napi::String>().Utf8Value();
        codecContextObject->hw_device_value = av_hwdevice_find_type_by_name(hardwareDeviceName.c_str());
        if (codecContextObject->hw_device_value == AV_HWDEVICE_TYPE_NONE)
        {
            Napi::Error::New(env, "Hardware device not found").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // hwaccel decoder name (ie, hevc_qsv, which needs to be manually specified for some reason)
        if (info.Length() > 2)
        {
            if (!info[2].IsUndefined() && !info[2].IsNull())
            {
                if (!info[2].IsString())
                {
                    Napi::Error::New(env, "Unexpected type in place of decoder string").ThrowAsJavaScriptException();
                    return env.Undefined();
                }

                codec = avcodec_find_decoder_by_name(info[2].As<Napi::String>().Utf8Value().c_str());
                if (!codec)
                {
                    Napi::Error::New(env, "Decoder not found").ThrowAsJavaScriptException();
                    return env.Undefined();
                }
            }
        }

        // specific device name (ie, /dev/dri/renderD128)
        if (info.Length() > 3)
        {
            if (!info[3].IsUndefined() && !info[3].IsNull())
            {
                if (!info[3].IsString())
                {
                    Napi::Error::New(env, "Unexpected type in place of device string").ThrowAsJavaScriptException();
                    return env.Undefined();
                }

                deviceName = info[3].As<Napi::String>().Utf8Value();
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
                                          deviceName.length() ? deviceName.c_str() : nullptr, NULL, 0)) < 0)
        {
            // throw
            Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
            return env.Undefined();
        }
    }

    codecContextObject->codecContext = avcodec_alloc_context3(codec);
    codecContextObject->codecContext->time_base = stream->time_base;
    codecContextObject->codecContext->opaque = codecContextObject;
    if (hw_device_ctx)
    {
        codecContextObject->codecContext->get_format = get_hw_format;
        codecContextObject->codecContext->hw_device_ctx = hw_device_ctx;
    }

    if ((ret = avcodec_parameters_to_context(codecContextObject->codecContext, fmt_ctx_->streams[streamIndex]->codecpar)) < 0)
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
        if (is_input) {
            avformat_close_input(&fmt_ctx_);
        }
        if (callbackRef) {
            callbackRef.Release();
        }
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

// Custom write function to intercept RTP packet data
static int write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    AVFormatContextObject *formatContextObject = (AVFormatContextObject *)opaque;

    // malloc and copy buf
    uint8_t *copy = (uint8_t *)av_malloc(buf_size);
    if (!copy)
    {
        return AVERROR(ENOMEM);
    }
    memcpy(copy, buf, buf_size);

    // Call the JavaScript function on the main thread
    formatContextObject->callbackRef.BlockingCall([copy, buf_size](Napi::Env env, Napi::Function jsCallback)
                                                  {
                                                      Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, copy, buf_size);
                                                      av_free(copy);
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
        int MAX_RTP_PACKET_SIZE = 64000;
        fmt_ctx_->packet_size = MAX_RTP_PACKET_SIZE;
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
        Napi::Error::New(env, "WriteFrame received null frame").ThrowAsJavaScriptException();
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
    Napi::Value formatContextValue = options.Get("formatContext");
    Napi::Value streamIndexValue = options.Get("streamIndex");
    AVStream *stream;

    if (codecContextValue.IsObject())
    {
        AVCodecContextObject *codecContextObject = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(codecContextValue.As<Napi::Object>());
        AVCodecContext *codecContext = codecContextObject->codecContext;
    
        // Create a new stream
        stream = avformat_new_stream(fmt_ctx_, NULL);
        if (!stream)
        {
            Napi::Error::New(env, "Failed to create new stream").ThrowAsJavaScriptException();
            return env.Undefined();
        }
    
        // Set the codec parameters for the new stream
        AVCodecParameters *codecpar = stream->codecpar;
        stream->time_base.num = codecContext->time_base.num;
        stream->time_base.den = codecContext->time_base.den;
    
        avcodec_parameters_from_context(codecpar, codecContext);
    }
    else if (formatContextValue.IsObject())
    {
        if (!streamIndexValue.IsNumber())
        {
            Napi::TypeError::New(env, "Number expected for streamIndex").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        unsigned int streamIndex = streamIndexValue.As<Napi::Number>().Uint32Value();
        AVFormatContextObject *formatContextObject = Napi::ObjectWrap<AVFormatContextObject>::Unwrap(formatContextValue.As<Napi::Object>());
        AVFormatContext *formatContext = formatContextObject->fmt_ctx_;
        if (!formatContext)
        {
            Napi::Error::New(env, "Format context is null").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (streamIndex >= formatContext->nb_streams)
        {
            Napi::Error::New(env, "Invalid stream index").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        AVStream *sourceStream = formatContext->streams[streamIndex];
        stream = avformat_new_stream(fmt_ctx_, NULL);
        if (!stream)
        {
            Napi::Error::New(env, "Failed to create new stream").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        AVCodecParameters *codecpar = stream->codecpar;
        if (avcodec_parameters_copy(codecpar, sourceStream->codecpar) < 0)
        {
            Napi::Error::New(env, "Failed to copy codec parameters").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        stream->time_base.num = sourceStream->time_base.num;
        stream->time_base.den = sourceStream->time_base.den;
    }
    else if (streamIndexValue.IsNumber())
    {
        int streamIndex = streamIndexValue.As<Napi::Number>().Int32Value();
        stream = fmt_ctx_->streams[streamIndex];
    }
    else {
        // throw
        Napi::Error::New(env, "Object expected for codecContext or formatContext").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int ret;
    if ((ret = avformat_write_header(fmt_ctx_, NULL)) < 0)
    {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return Napi::Number::New(env, stream->index);
}

Napi::Value AVFormatContextObject::GetStreams(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Array streams = Napi::Array::New(env);

    if (!fmt_ctx_)
    {
        return streams;
    }

    for (unsigned int i = 0; i < fmt_ctx_->nb_streams; i++)
    {
        AVStream *s = fmt_ctx_->streams[i];
        Napi::Object stream = Napi::Object::New(env);
        stream.Set("index", Napi::Number::New(env, s->index));
        stream.Set("codec", Napi::String::New(env, avcodec_get_name(s->codecpar->codec_id)));
        stream.Set("type", Napi::String::New(env, av_get_media_type_string(s->codecpar->codec_type)));
        stream.Set("timeBaseNum", Napi::Number::New(env, s->time_base.num));
        stream.Set("timeBaseDen", Napi::Number::New(env, s->time_base.den));
        streams.Set(i, stream);
    }

    return streams;
}

Napi::Value AVFormatContextObject::CreateSDP(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!fmt_ctx_)
    {
        Napi::Error::New(env, "Format context is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    char sdp[65536];
    // create array of 1 AVFormatContext
    AVFormatContext *ctx[1] = {fmt_ctx_};
    int ret = av_sdp_create(ctx, 1, sdp, sizeof(sdp));
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::String sdpString = Napi::String::New(env, sdp);
    return sdpString;
}

Napi::Value createSDP(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Array of AVFormatContext expected for argument 0").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Array formatContextArray = info[0].As<Napi::Array>();
    size_t length = formatContextArray.Length();
    AVFormatContext *ctx[100];

    if (length > 100) {
        Napi::Error::New(env, "Array length must be less than 100").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    for (size_t i = 0; i < length; i++) {
        Napi::Object formatContextObject = formatContextArray.Get(i).As<Napi::Object>();
        AVFormatContextObject *avFormatContextObject = Napi::ObjectWrap<AVFormatContextObject>::Unwrap(formatContextObject);
        ctx[i] = avFormatContextObject->fmt_ctx_;
    }

    char sdp[65536];
    int ret = av_sdp_create(ctx, length, sdp, sizeof(sdp));
    if (ret < 0) {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::String sdpString = Napi::String::New(env, sdp);
    return sdpString;
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
    AVBitstreamFilter::Init(env, exports);

    exports.Set(Napi::String::New(env, "setLogLevel"), Napi::Function::New(env, setLogLevel));
    exports.Set(Napi::String::New(env, "createSdp"), Napi::Function::New(env, createSDP));
    return exports;
}

NODE_API_MODULE(addon, Init)
