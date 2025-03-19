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

                                                                AVFrameObject::InstanceAccessor("pts", &AVFrameObject::GetPTS, &AVFrameObject::SetPTS),

                                                                AVFrameObject::InstanceAccessor("dts", &AVFrameObject::GetDTS, &AVFrameObject::SetDTS),

                                                                AVFrameObject::InstanceAccessor("hardwareDeviceType", &AVFrameObject::GetHardware, NULL),

                                                                AVFrameObject::InstanceAccessor("softwareFormat", &AVFrameObject::GetSoftware, NULL),

                                                                AVFrameObject::InstanceAccessor("flags", &AVFrameObject::GetFlags, &AVFrameObject::SetFlags),

                                                                AVFrameObject::InstanceAccessor("pictType", &AVFrameObject::GetPictType, &AVFrameObject::SetPictType),

                                                                InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVFrameObject::Destroy),

                                                                InstanceMethod("destroy", &AVFrameObject::Destroy),

                                                                InstanceMethod("toBuffer", &AVFrameObject::ToBuffer),

                                                                InstanceMethod("fromBuffer", &AVFrameObject::FromBuffer),

                                                                InstanceMethod("createEncoder", &AVFrameObject::CreateEncoder),

                                                            });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVFrame", func);
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
    if (info.Length())
    {
        if (info.Length() < 3)
        {
            Napi::TypeError::New(info.Env(), "Expected 3 arguments: width, height, pixelFormat").ThrowAsJavaScriptException();
            return;
        }

        if (!info[0].IsNumber())
        {
            Napi::TypeError::New(info.Env(), "Number expected for width").ThrowAsJavaScriptException();
            return;
        }
        int width = info[0].As<Napi::Number>().Int32Value();

        if (!info[1].IsNumber())
        {
            Napi::TypeError::New(info.Env(), "Number expected for height").ThrowAsJavaScriptException();
            return;
        }

        int height = info[1].As<Napi::Number>().Int32Value();

        if (!info[2].IsString())
        {
            Napi::TypeError::New(info.Env(), "String expected for pixel format").ThrowAsJavaScriptException();
            return;
        }

        bool fillBlack = true;
        if (info.Length() > 3 && info[3].IsBoolean())
        {
            fillBlack = info[3].As<Napi::Boolean>().Value();
        }

        std::string pixelFormat = info[2].As<Napi::String>().Utf8Value();
        AVPixelFormat format = av_get_pix_fmt(pixelFormat.c_str());
        if (format == AV_PIX_FMT_NONE)
        {
            Napi::Error::New(info.Env(), "Invalid pixel format").ThrowAsJavaScriptException();
            return;
        }

        // construct frame
        frame_ = av_frame_alloc();
        if (!frame_)
        {
            Napi::Error::New(info.Env(), "Could not allocate frame").ThrowAsJavaScriptException();
            return;
        }

        frame_->width = width;
        frame_->height = height;
        frame_->format = format;

        int ret = av_frame_get_buffer(frame_, 0);
        if (ret < 0)
        {
            av_frame_free(&frame_);
            Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        }

        if (fillBlack)
        {
            ptrdiff_t linesizes[AV_NUM_DATA_POINTERS];
            for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
            {
                linesizes[i] = (ptrdiff_t)frame_->linesize[i];
            }
            av_image_fill_black(frame_->data, linesizes, format, frame_->color_range, frame_->width, frame_->height);
        }
    }
}

AVFrameObject::~AVFrameObject()
{
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
    Napi::Value maxRateValue = options.Get("maxRate");
    if (maxRateValue.IsNumber())
    {
        c->rc_max_rate = maxRateValue.As<Napi::Number>().Int32Value();
    }
    else {
        c->rc_max_rate = c->bit_rate;
    }

    Napi::Value bufSizeValue = options.Get("bufSize");
    if (bufSizeValue.IsNumber())
    {
        c->rc_buffer_size = bufSizeValue.As<Napi::Number>().Int32Value();
    }
    else {
        c->rc_buffer_size = c->bit_rate * 2;
    }

    c->width = frame_->width;
    c->height = frame_->height;
    c->pix_fmt = (enum AVPixelFormat)frame_->format;
    c->time_base.num = timeBaseNum;
    c->time_base.den = timeBaseDen;
    c->max_b_frames = 0;

    Napi::Value profileValue = options.Get("profile");
    // if number, set it
    if (profileValue.IsNumber())
    {
        c->profile = profileValue.As<Napi::Number>().Int32Value();
    }

    Napi::Value gopSizeValue = options.Get("gopSize");
    if (gopSizeValue.IsNumber()) {
        c->gop_size = gopSizeValue.As<Napi::Number>().Int32Value();
    }

    Napi::Value keyintMinValue = options.Get("keyIntMin");
    if (keyintMinValue.IsNumber()) {
        c->keyint_min = keyintMinValue.As<Napi::Number>().Int32Value();
    }


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

Napi::Value AVFrameObject::FromBuffer(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        Napi::Error::New(env, "Frame object is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (frame_->hw_frames_ctx)
    {
        return env.Undefined();
    }

    if (!frame_->data[0])
    {
        Napi::Error::New(env, "Frame data is null").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsBuffer())
    {
        Napi::TypeError::New(env, "Buffer expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    if ((int)buffer.Length() != av_image_get_buffer_size((enum AVPixelFormat)frame_->format, frame_->width, frame_->height, 1))
    {
        Napi::Error::New(env, "Buffer size does not match frame size").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int ret = av_image_fill_arrays(frame_->data, frame_->linesize, buffer.Data(), (enum AVPixelFormat)frame_->format, frame_->width, frame_->height, 1);
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
    }

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

    if (frame_->hw_frames_ctx)
    {
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

    int ret = av_image_copy_to_buffer(byte_array, buffer_size, (const uint8_t *const *)frame_->data, frame_->linesize, (enum AVPixelFormat)frame_->format, width, height, 1);
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return env.Undefined();
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

Napi::Value AVFrameObject::GetPTS(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->pts);
}

void AVFrameObject::SetPTS(const Napi::CallbackInfo &info, const Napi::Value &value)
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
    frame_->pts = value.As<Napi::Number>().Int64Value();
}

Napi::Value AVFrameObject::GetDTS(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->pkt_dts);
}

void AVFrameObject::SetDTS(const Napi::CallbackInfo &info, const Napi::Value &value)
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
    frame_->pkt_dts = value.As<Napi::Number>().Int64Value();
}

Napi::Value AVFrameObject::GetHardware(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    if (frame_->hw_frames_ctx)
    {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(frame_->hw_frames_ctx->data);
        AVBufferRef *hw_device_ctx = frames_ctx->device_ref;
        if (hw_device_ctx)
        {
            AVHWDeviceContext *device_ctx = (AVHWDeviceContext *)(hw_device_ctx->data);
            // type to string
            std::string type = av_hwdevice_get_type_name(device_ctx->type);
            return Napi::String::New(env, type.c_str());
        }
    }
    return env.Null();
}

Napi::Value AVFrameObject::GetSoftware(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }

    if (frame_->hw_frames_ctx)
    {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(frame_->hw_frames_ctx->data);
        std::string format = av_get_pix_fmt_name(frames_ctx->sw_format);
        return Napi::String::New(env, format.c_str());
    }
    return env.Null();
}

Napi::Value AVFrameObject::GetFlags(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->flags);
}

void AVFrameObject::SetFlags(const Napi::CallbackInfo &info, const Napi::Value &value)
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
    frame_->flags = value.As<Napi::Number>().Int32Value();
}

Napi::Value AVFrameObject::GetPictType(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (!frame_)
    {
        return env.Undefined();
    }
    return Napi::Number::New(env, frame_->pict_type);
}

void AVFrameObject::SetPictType(const Napi::CallbackInfo &info, const Napi::Value &value)
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
    frame_->pict_type = (enum AVPictureType)value.As<Napi::Number>().Int32Value();
}
