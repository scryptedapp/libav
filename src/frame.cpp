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

class AVFrameObject : public Napi::ObjectWrap<AVFrameObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AVFrameObject(const Napi::CallbackInfo &info);
    ~AVFrameObject(); // Explicitly declare the destructor

    // Factory method to create an instance from C++
    static Napi::Object NewInstance(Napi::Env env, AVFrame *frame);
    AVFrame *frame_;

private:
    static Napi::FunctionReference constructor;
    Napi::Value GetWidth(const Napi::CallbackInfo &info);
    Napi::Value GetHeight(const Napi::CallbackInfo &info);
    Napi::Value GetFormat(const Napi::CallbackInfo &info);

    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value ToJPEG(const Napi::CallbackInfo &info);
    Napi::Value ToBuffer(const Napi::CallbackInfo &info);
};

Napi::FunctionReference AVFrameObject::constructor;

Napi::Object AVFrameObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVFrameObject", {

                                                                InstanceMethod("close", &AVFrameObject::Close),

                                                                InstanceMethod("toBuffer", &AVFrameObject::ToBuffer),

                                                                InstanceMethod("toJpeg", &AVFrameObject::ToJPEG),

                                                                AVFrameObject::InstanceAccessor("width", &AVFrameObject::GetWidth, nullptr),

                                                                AVFrameObject::InstanceAccessor("height", &AVFrameObject::GetHeight, nullptr),

                                                                AVFrameObject::InstanceAccessor("format", &AVFrameObject::GetFormat, nullptr),

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
    if (frame_)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
}

Napi::Value AVFrameObject::ToJPEG(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Null();
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec)
    {
        Napi::Error::New(env, "Failed to find MJPEG codec").ThrowAsJavaScriptException();
        return env.Null();
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c)
    {
        Napi::Error::New(env, "Failed to allocate codec context").ThrowAsJavaScriptException();
        return env.Null();
    }

    c->bit_rate = 400000; // This is generally less relevant for single images
    c->width = frame_->width;
    c->height = frame_->height;
    c->pix_fmt = (enum AVPixelFormat)frame_->format;
    c->time_base = (AVRational){1, 25};

    // Set the JPEG quality
    int quality = 2; // 1-31, lower is better quality
    av_opt_set_int(c, "qscale", quality, 0); // Set the quality scale (1-31, lower is better quality)
    av_opt_set_int(c, "qmin", quality, 0);   // Set the minimum quality
    av_opt_set_int(c, "qmax", quality, 0);   // Set the maximum quality

    if (avcodec_open2(c, codec, NULL) < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, "Failed to open codec").ThrowAsJavaScriptException();
        return env.Null();
    }

    int ret = avcodec_send_frame(c, frame_);
    if (ret < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, "Failed to send frame").ThrowAsJavaScriptException();
        return env.Null();
    }

    AVPacket packet = { 0 };
    pkt.data = NULL; // Packet data will be allocated by the encoder
    pkt.size = 0;

    ret = avcodec_receive_packet(c, &pkt);
    if (ret < 0)
    {
        avcodec_free_context(&c);
        Napi::Error::New(env, "Failed to receive packet").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, pkt.data, pkt.size);
    av_packet_unref(&pkt);
    avcodec_free_context(&c);

    return buffer;
}

Napi::Value AVFrameObject::ToBuffer(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Calculate the required buffer size
    int width = frame_->width;
    int height = frame_->height;
    int buffer_size = av_image_get_buffer_size((enum AVPixelFormat)frame_->format, width, height, 1);

    // Allocate memory for the byte array
    uint8_t *byte_array = (uint8_t *)malloc(buffer_size);
    if (!byte_array)
    {
        fprintf(stderr, "Failed to allocate memory for byte array\n");
        return env.Null();
    }

    // Copy the frame data to the byte array
    int plane_size = width * 3; // Width * 3 bytes per pixel
    for (int y = 0; y < height; y++)
    {
        memcpy(byte_array + y * plane_size, frame_->data[0] + y * frame_->linesize[0], plane_size);
    }

    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, byte_array, buffer_size);

    return buffer;
}

Napi::Value AVFrameObject::Close(const Napi::CallbackInfo &info)
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
        return env.Null();
    }
    return Napi::Number::New(env, frame_->width);
}

Napi::Value AVFrameObject::GetHeight(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Null();
    }
    return Napi::Number::New(env, frame_->height);
}

Napi::Value AVFrameObject::GetFormat(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Null();
    }
    enum AVPixelFormat pix_fmt = (enum AVPixelFormat)frame_->format;
    const char *format = av_get_pix_fmt_name(pix_fmt);
    return Napi::String::New(env, format);
}
