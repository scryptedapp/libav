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

class AVPacketObject : public Napi::ObjectWrap<AVPacketObject>
{
public:
    static Napi::FunctionReference constructor;
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Object NewInstance(Napi::Env env, AVPacket *packet);

    AVPacketObject(const Napi::CallbackInfo &info);
    ~AVPacketObject();
    Napi::Value Clone(const Napi::CallbackInfo &info);
    Napi::Value Destroy(const Napi::CallbackInfo &info);
    Napi::Value GetIsKeyFrame(const Napi::CallbackInfo &info);

    AVPacket *packet;
};

Napi::FunctionReference AVPacketObject::constructor;

Napi::Object AVPacketObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVPacketObject", {

                                                                 AVPacketObject::InstanceAccessor("isKeyFrame", &AVPacketObject::GetIsKeyFrame, nullptr),

                                                                 InstanceMethod("clone", &AVPacketObject::Clone),

                                                                 InstanceMethod("destroy", &AVPacketObject::Destroy),

                                                             });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    return exports;
}

Napi::Object AVPacketObject::NewInstance(Napi::Env env, AVPacket *packet)
{
    Napi::Object obj = constructor.New({});
    AVPacketObject *avPacketObject = Napi::ObjectWrap<AVPacketObject>::Unwrap(obj);
    avPacketObject->packet = packet;
    return obj;
}

AVPacketObject::AVPacketObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVPacketObject>(info)
{
    packet = nullptr;
}

AVPacketObject::~AVPacketObject()
{
}

Napi::Value AVPacketObject::Clone(const Napi::CallbackInfo &info)
{
    if (!packet)
    {
        return info.Env().Undefined();
    }

    AVPacket *newPacket = av_packet_alloc();
    if (!newPacket)
    {
        Napi::TypeError::New(info.Env(), "Failed to allocate new AVPacket").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    int ret = av_packet_ref(newPacket, packet);
    if (ret < 0)
    {
        av_packet_free(&newPacket);
        Napi::TypeError::New(info.Env(), "Failed to reference AVPacket").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return NewInstance(info.Env(), newPacket);
}

Napi::Value AVPacketObject::Destroy(const Napi::CallbackInfo &info)
{
    if (packet)
    {
        av_packet_free(&packet);
        packet = nullptr;
    }

    return info.Env().Undefined();
}

Napi::Value AVPacketObject::GetIsKeyFrame(const Napi::CallbackInfo &info)
{
    bool keyFrame = packet ? packet->flags & AV_PKT_FLAG_KEY : false;
    return Napi::Boolean::New(info.Env(), keyFrame);
}
