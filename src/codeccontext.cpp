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

#ifdef __linux__
#include <libavutil/hwcontext_vaapi.h>
    extern const char *vaQueryVendorString(void *dpy);
#endif
}

#include <thread>
#include <v8.h>

#include "packet.h"
#include "error.h"
#include "codeccontext.h"
#include "frame.h"
#include "receive-frame-worker.h"
#include "receive-packet-worker.h"
#include "send-frame-worker.h"
#include "send-packet-worker.h"

Napi::FunctionReference AVCodecContextObject::constructor;

AVCodecContextObject::AVCodecContextObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVCodecContextObject>(info),
      codecContext(nullptr),
      hw_device_value(AV_HWDEVICE_TYPE_NONE)
{
    // i don't think this constructor is called from js??
}

AVCodecContextObject::~AVCodecContextObject()
{
}

Napi::Value AVCodecContextObject::Destroy(const Napi::CallbackInfo &info)
{
    if (codecContext)
    {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }

    return info.Env().Undefined();
}

Napi::Object AVCodecContextObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVCodecContextObject", {

                                                                       AVCodecContextObject::InstanceAccessor("hardwareDevice", &AVCodecContextObject::GetHardwareDevice, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("pixelFormat", &AVCodecContextObject::GetPixelFormat, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("hardwarePixelFormat", &AVCodecContextObject::GetHardwarePixelFormat, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("timeBaseNum", &AVCodecContextObject::GetTimeBaseNum, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("timeBaseDen", &AVCodecContextObject::GetTimeBaseDen, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("vendorInfo", &AVCodecContextObject::GetVendorInfo, nullptr),

                                                                       AVCodecContextObject::InstanceAccessor("keyIntMin", &AVCodecContextObject::GetKeyIntMin, &AVCodecContextObject::SetKeyIntMin),

                                                                       AVCodecContextObject::InstanceAccessor("gopSize", &AVCodecContextObject::GetGopSize, &AVCodecContextObject::SetGopSize),

                                                                       InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVCodecContextObject::Destroy),

                                                                       InstanceMethod("destroy", &AVCodecContextObject::Destroy),

                                                                       InstanceMethod("sendPacket", &AVCodecContextObject::SendPacket),

                                                                       InstanceMethod("receiveFrame", &AVCodecContextObject::ReceiveFrame),

                                                                       InstanceMethod("sendFrame", &AVCodecContextObject::SendFrame),

                                                                       InstanceMethod("receivePacket", &AVCodecContextObject::ReceivePacket),
                                                                   });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    return exports;
}

// Factory method to create an instance from C++
Napi::Object AVCodecContextObject::NewInstance(Napi::Env env)

{
    Napi::Object obj = constructor.New({});
    AVCodecContextObject *wrapper = Napi::ObjectWrap<AVCodecContextObject>::Unwrap(obj);
    wrapper->codecContext = nullptr;
    return obj;
}


Napi::Value AVCodecContextObject::ReceiveFrame(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    ReceiveFrameWorker *worker = new ReceiveFrameWorker(env, deferred, this);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVCodecContextObject::ReceivePacket(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    ReceivePacketWorker *worker = new ReceivePacketWorker(env, deferred, codecContext);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVCodecContextObject::SendPacket(const Napi::CallbackInfo &info)
{
    if (!info.Length())
    {
        Napi::TypeError::New(info.Env(), "Packet object expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVPacketObject *packet = Napi::ObjectWrap<AVPacketObject>::Unwrap(info[0].As<Napi::Object>());

    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    SendPacketWorker *worker = new SendPacketWorker(env, deferred, this, packet);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVCodecContextObject::SendFrame(const Napi::CallbackInfo &info)
{
    if (!info.Length() || !info[0].IsObject())
    {
        Napi::TypeError::New(info.Env(), "Frame object expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVFrameObject *frame = Napi::ObjectWrap<AVFrameObject>::Unwrap(info[0].As<Napi::Object>());

    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    SendFrameWorker *worker = new SendFrameWorker(env, deferred, this, frame);
    worker->Queue();

    // Return the promise to JavaScript
    return Napi::Value(env, promise);
}

Napi::Value AVCodecContextObject::GetHardwareDevice(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    const char *hardwareDeviceName = av_hwdevice_get_type_name(hw_device_value);
    if (hw_device_value == AV_HWDEVICE_TYPE_NONE || !hardwareDeviceName)
    {
        return info.Env().Undefined();
    }
    return Napi::String::New(env, hardwareDeviceName);
}

Napi::Value AVCodecContextObject::GetPixelFormat(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    const char *name = av_get_pix_fmt_name(codecContext->pix_fmt);
    if (!name)
    {
        return env.Undefined();
    }
    return Napi::String::New(env, name);
}

Napi::Value AVCodecContextObject::GetHardwarePixelFormat(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    const char *name = av_get_pix_fmt_name(hw_pix_fmt);
    if (!name)
    {
        return env.Undefined();
    }
    return Napi::String::New(env, name);
}

Napi::Value AVCodecContextObject::GetTimeBaseNum(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    return Napi::Number::New(env, codecContext->time_base.num);
}

Napi::Value AVCodecContextObject::GetTimeBaseDen(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    return Napi::Number::New(env, codecContext->time_base.den);
}

Napi::Value AVCodecContextObject::GetVendorInfo(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // compiler check for linux x64 to query vaapi
#ifndef __x86_64__
    return env.Undefined();
#endif
#ifndef __linux__
    return env.Undefined();
#else

    if (!codecContext)
    {
        return env.Undefined();
    }

    const char *name = av_get_pix_fmt_name(hw_pix_fmt);
    if (!name || strcmp(name, "vaapi"))
    {
        return env.Undefined();
    }

    if (!codecContext->hw_device_ctx || !codecContext->hw_device_ctx->data)
    {
        return env.Undefined();
    }
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext*)codecContext->hw_device_ctx->data;

    AVVAAPIDeviceContext *vaapi_device_ctx = (AVVAAPIDeviceContext *)hw_device_ctx->hwctx;
    if (!vaapi_device_ctx)
    {
        return env.Undefined();
    }

    void *display = vaapi_device_ctx->display;
    if (!display) {
        return env.Undefined();
    }
    const char *driver = vaQueryVendorString(display);
    if (!driver)
    {
        return env.Undefined();
    }

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("driver", Napi::String::New(env, driver));
    return obj;
#endif
}

Napi::Value AVCodecContextObject::GetKeyIntMin(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    return Napi::Number::New(env, codecContext->keyint_min);
}

void AVCodecContextObject::SetKeyIntMin(const Napi::CallbackInfo &info, const Napi::Value &value)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return;
    }

    if (!value.IsNumber())
    {
        Napi::TypeError::New(env, "keyIntMin must be a number").ThrowAsJavaScriptException();
        return;
    }

    codecContext->keyint_min = value.As<Napi::Number>().Int32Value();
}

Napi::Value AVCodecContextObject::GetGopSize(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return env.Undefined();
    }

    return Napi::Number::New(env, codecContext->gop_size);
}

void AVCodecContextObject::SetGopSize(const Napi::CallbackInfo &info, const Napi::Value &value)
{
    Napi::Env env = info.Env();

    if (!codecContext)
    {
        return;
    }

    if (!value.IsNumber())
    {
        Napi::TypeError::New(env, "gopSize must be a number").ThrowAsJavaScriptException();
        return;
    }

    codecContext->gop_size = value.As<Napi::Number>().Int32Value();
}
