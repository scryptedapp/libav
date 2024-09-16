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

#include "packet.cpp"
#include "error.cpp"

class AVCodecContextObject : public Napi::ObjectWrap<AVCodecContextObject>
{
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Object NewInstance(Napi::Env env);
    static Napi::FunctionReference constructor;

    AVCodecContextObject(const Napi::CallbackInfo &info);
    ~AVCodecContextObject(); // Explicitly declare the destructor

    enum AVPixelFormat hw_pix_fmt;
    int videoStreamIndex;
    AVCodecContext *codecContext;
    enum AVHWDeviceType hw_device_value;

private:
    Napi::Value GetHardwareDevice(const Napi::CallbackInfo &info);
    Napi::Value GetPixelFormat(const Napi::CallbackInfo &info);
    Napi::Value GetHardwarePixelFormat(const Napi::CallbackInfo &info);
    Napi::Value ReceiveFrame(const Napi::CallbackInfo &info);
    Napi::Value SendPacket(const Napi::CallbackInfo &info);
    Napi::Value Destroy(const Napi::CallbackInfo &info);
};

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

                                                                       InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVCodecContextObject::Destroy),

                                                                       InstanceMethod("destroy", &AVCodecContextObject::Destroy),

                                                                       InstanceMethod("sendPacket", &AVCodecContextObject::SendPacket),

                                                                       InstanceMethod("receiveFrame", &AVCodecContextObject::ReceiveFrame),
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

        // attempt to read frames first, EAGAIN will be returned if data needs to be
        // sent to decoder.
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

        // errors are typically caused by missing codec info
        // or invalid packets, and may resolve on a later packet.
        // suppress all errors until a keyframe is sent.
        if (ret)
        {
            SetError(AVErrorString(ret));
        }
    }

    // This method runs in the main thread after Execute completes successfully
    void OnOK() override
    {
        Napi::Env env = Env();
        napi_resolve_deferred(Env(), deferred, env.Undefined());
    }

    // This method runs in the main thread if Execute fails
    void OnError(const Napi::Error &e) override
    {
        napi_value error = e.Value();

        // Reject the promise
        napi_reject_deferred(Env(), deferred, error);
    }

private:
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
