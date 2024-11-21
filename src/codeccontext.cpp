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
    extern const char *vaQueryVendorString(void *dpy);
#endif
}

#include <thread>
#include <v8.h>

#include "packet.h"
#include "error.h"
#include "codeccontext.h"
#include "frame.h"

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

class ReceiveFrameWorker : public Napi::AsyncWorker
{
public:
    ReceiveFrameWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext)
        : Napi::AsyncWorker(env), deferred(deferred), codecContext(codecContext)
    {
    }

    void Execute() override
    {
        AVFrame *frame = av_frame_alloc();
        if (!frame)
        {
            SetError("Failed to allocate frame");
            return;
        }

        //  EAGAIN will be returned if packets need to be sent to decoder.
        int ret = avcodec_receive_frame(codecContext, frame);
        if (!ret)
        {
            result = frame;
            return;
        }

        av_frame_free(&frame);
        if (ret != AVERROR(EAGAIN))
        {
            SetError(AVErrorString(ret));
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
    AVCodecContext *codecContext;
};

class ReceivePacketWorker : public Napi::AsyncWorker
{
public:
    ReceivePacketWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext)
        : Napi::AsyncWorker(env), deferred(deferred), codecContext(codecContext)
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

        // EAGAIN will be returned if frames needs to be sent to encoder
        int ret = avcodec_receive_packet(codecContext, packet);
        if (!ret)
        {
            result = packet;
            return;
        }

        av_packet_free(&packet);
        if (ret != AVERROR(EAGAIN))
        {
            SetError(AVErrorString(ret));
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
    AVCodecContext *codecContext;
};

Napi::Value AVCodecContextObject::ReceiveFrame(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    // Create a new promise and get its deferred handle
    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Create and queue the AsyncWorker, passing the deferred handle
    ReceiveFrameWorker *worker = new ReceiveFrameWorker(env, deferred, codecContext);
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

class SendFrameWorker : public Napi::AsyncWorker
{
public:
    SendFrameWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext, AVFrame *frame)
        : Napi::AsyncWorker(env), deferred(deferred), codecContext(codecContext), frame(frame)
    {
    }

    void Execute() override
    {
        if (!frame)
        {
            SetError("Packet is null");
            return;
        }
        if (!codecContext)
        {
            SetError("Codec Context is null");
            return;
        }

        int ret = avcodec_send_frame(codecContext, frame);

        if (!ret)
        {
            result = true;
            return;
        }

        result = false;

        if (ret != AVERROR(EAGAIN))
        {
            // what would cause this?
            SetError(AVErrorString(ret));
        }
    }

    // This method runs in the main thread after Execute completes successfully
    void OnOK() override
    {
        Napi::Env env = Env();
        napi_resolve_deferred(env, deferred, Napi::Boolean::New(env, result));
    }

    // This method runs in the main thread if Execute fails
    void OnError(const Napi::Error &e) override
    {
        napi_value error = e.Value();

        // Reject the promise
        napi_reject_deferred(Env(), deferred, error);
    }

private:
    bool result;
    napi_deferred deferred;
    AVCodecContext *codecContext;
    AVFrame *frame;
};

class SendPacketWorker : public Napi::AsyncWorker
{
public:
    SendPacketWorker(napi_env env, napi_deferred deferred, AVCodecContext *codecContext, AVPacket *packet)
        : Napi::AsyncWorker(env), deferred(deferred), codecContext(codecContext), packet(packet)
    {
    }

    void Execute() override
    {
        if (!packet)
        {
            SetError("Packet is null");
            return;
        }

        int ret = avcodec_send_packet(codecContext, packet);

        if (!ret)
        {
            result = true;
            return;
        }

        result = false;

        if (ret != AVERROR(EAGAIN))
        {
            // errors are typically caused by missing codec info
            // or invalid packets, and may resolve on a later packet.
            // suppress all errors until a keyframe is sent.
            SetError(AVErrorString(ret));
        }
    }

    // This method runs in the main thread after Execute completes successfully
    void OnOK() override
    {
        Napi::Env env = Env();
        napi_resolve_deferred(env, deferred, Napi::Boolean::New(env, result));
    }

    // This method runs in the main thread if Execute fails
    void OnError(const Napi::Error &e) override
    {
        napi_value error = e.Value();

        // Reject the promise
        napi_reject_deferred(Env(), deferred, error);
    }

private:
    bool result;
    napi_deferred deferred;
    AVCodecContext *codecContext;
    AVPacket *packet;
};

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
    SendPacketWorker *worker = new SendPacketWorker(env, deferred, codecContext, packet->packet);
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
    SendFrameWorker *worker = new SendFrameWorker(env, deferred, codecContext, frame->frame_);
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
    if (strcmp(name, "vaapi"))
    {
        return env.Undefined();
    }

    AVBufferRef *hw_device_ctx = codecContext->hw_device_ctx;
    if (!hw_device_ctx)
    {
        return env.Undefined();
    }
    AVVAAPIDeviceContext *vaapi_device_ctx = (AVHWDeviceContext *)hw_device_ctx->data;
    if (!vaapi_device_ctx)
    {
        return env.Undefined();
    }

    void *display = vaapi_device_ctx->display;
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
