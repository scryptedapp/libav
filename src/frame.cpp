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

                                                                AVFrameObject::InstanceAccessor("timeBaseNum", &AVFrameObject::GetTimeBaseNum, &AVFrameObject::SetTimeBaseNum),

                                                                AVFrameObject::InstanceAccessor("timeBaseDen", &AVFrameObject::GetTimeBaseDen, &AVFrameObject::SetTimeBaseDen),

                                                                InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFrameObject::Destroy),

                                                                InstanceMethod("destroy", &AVFrameObject::Destroy),

                                                                InstanceMethod("toBuffer", &AVFrameObject::ToBuffer),

                                                                InstanceMethod("toJpeg", &AVFrameObject::ToJPEG),

                                                                InstanceMethod("createEncoder", &AVFrameObject::CreateEncoder),

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

    Napi::Value timebaseValue = options.Get("timeBase");
    if (!timebaseValue.IsObject())
    {
        Napi::TypeError::New(env, "Object expected for timeBase").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Object timebaseObject = timebaseValue.As<Napi::Object>();
    // validate the timebase object
    if (!timebaseObject.Has("timeBaseNum") || !timebaseObject.Get("timeBaseNum").IsNumber() ||
        !timebaseObject.Has("timeBaseDen") || !timebaseObject.Get("timeBaseDen").IsNumber())
    {
        Napi::TypeError::New(env, "invalid object for timeBase").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int timeBaseNum = timebaseObject.Get("timeBaseNum").As<Napi::Number>().Int32Value();
    int timeBaseDen = timebaseObject.Get("timeBaseDen").As<Napi::Number>().Int32Value();

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
    c->time_base.num = timeBaseNum;
    c->time_base.den = timeBaseDen;

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

void AVFrameObject::SetTimeBaseNum(const Napi::CallbackInfo &info, const Napi::Value &value)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return;
    }
    if (!value.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return;
    }
    frame_->time_base.num = value.As<Napi::Number>().Int32Value();
}

void AVFrameObject::SetTimeBaseDen(const Napi::CallbackInfo &info, const Napi::Value &value)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return;
    }
    if (!value.IsNumber())
    {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return;
    }
    frame_->time_base.den = value.As<Napi::Number>().Int32Value();
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
