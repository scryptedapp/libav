#include "bsf.h"
#include "packet.h"
#include "formatcontext.h"

Napi::FunctionReference AVBitstreamFilterObject::constructor;

Napi::Object AVBitstreamFilterObject::Init(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AVBitstreamFilter", {

                                                                      InstanceMethod(Napi::Symbol::WellKnown(env, "dispose"), &AVBitstreamFilterObject::Destroy),

                                                                      InstanceMethod("destroy", &AVBitstreamFilterObject::Destroy),

                                                                      InstanceMethod("setOption", &AVBitstreamFilterObject::SetOption),

                                                                      InstanceMethod("sendPacket", &AVBitstreamFilterObject::SendPacket),

                                                                      InstanceMethod("copyParameters", &AVBitstreamFilterObject::CopyParameters),

                                                                      InstanceMethod("receivePacket", &AVBitstreamFilterObject::ReceivePacket)});

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AVBitstreamFilter", func);

    return exports;
}

AVBitstreamFilterObject::AVBitstreamFilterObject(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<AVBitstreamFilterObject>(info), bsfContext(nullptr)
{
    bsfContext = nullptr;

    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString())
    {
        Napi::TypeError::New(env, "String expected for argument 0: filter name").ThrowAsJavaScriptException();
        return;
    }

    std::string filterName = info[0].As<Napi::String>().Utf8Value();

    const AVBitStreamFilter *filter = av_bsf_get_by_name(filterName.c_str());
    if (!filter)
    {
        Napi::Error::New(env, "Bitstream filter not found").ThrowAsJavaScriptException();
        return;
    }

    int ret = av_bsf_alloc(filter, &bsfContext);
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return;
    }
}

AVBitstreamFilterObject::~AVBitstreamFilterObject()
{
}

Napi::Object AVBitstreamFilterObject::NewInstance(Napi::Env env, AVBSFContext *bsfContext)
{
    Napi::EscapableHandleScope scope(env);
    Napi::Object obj = constructor.New({});
    AVBitstreamFilterObject *instance = Napi::ObjectWrap<AVBitstreamFilterObject>::Unwrap(obj);
    instance->bsfContext = bsfContext;
    return scope.Escape(napi_value(obj)).ToObject();
}

Napi::Value AVBitstreamFilterObject::SendPacket(const Napi::CallbackInfo &info)
{
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(info.Env(), "Packet object expected").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVPacketObject *packetObject = Napi::ObjectWrap<AVPacketObject>::Unwrap(info[0].As<Napi::Object>());

    int ret = av_bsf_send_packet(bsfContext, packetObject->packet);
    if (ret < 0)
    {
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}

Napi::Value AVBitstreamFilterObject::ReceivePacket(const Napi::CallbackInfo &info)
{
    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        Napi::Error::New(info.Env(), "Could not allocate packet").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    int ret = av_bsf_receive_packet(bsfContext, packet);
    if (ret == AVERROR(EAGAIN))
    {
        av_packet_free(&packet);
        return info.Env().Undefined();
    }
    if (ret < 0)
    {
        av_packet_free(&packet);
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    Napi::Object packetObject = AVPacketObject::NewInstance(info.Env(), packet);
    return packetObject;
}

Napi::Value AVBitstreamFilterObject::SetOption(const Napi::CallbackInfo &info)
{
    if (!bsfContext)
    {
        Napi::TypeError::New(info.Env(), "Bitstream Filter object is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Object expected for argument 0: options").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    Napi::Object options = info[0].As<Napi::Object>();

    // convert this options to a AVDictionary
    AVDictionary *optionsDict = nullptr;

    Napi::Array keys = options.GetPropertyNames();
    for (unsigned int i = 0; i < keys.Length(); i++)
    {
        Napi::Value key = keys.Get(i);
        Napi::Value value = options.Get(key.ToString());

        if (value.IsString())
        {
            std::string keyStr = key.ToString();
            std::string valueStr = value.ToString();

            av_dict_set(&optionsDict, keyStr.c_str(), valueStr.c_str(), 0);
        }
    }

    int ret = av_opt_set_dict2(bsfContext, &optionsDict, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        Napi::Error::New(env, AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}

Napi::Value AVBitstreamFilterObject::Destroy(const Napi::CallbackInfo &info)
{
    if (bsfContext)
    {
        av_bsf_free(&bsfContext);
        bsfContext = nullptr;
    }
    return info.Env().Undefined();
}

Napi::Value AVBitstreamFilterObject::CopyParameters(const Napi::CallbackInfo &info)
{
    if (!bsfContext)
    {
        Napi::TypeError::New(info.Env(), "Bitstream Filter object is null").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    // get the AVFormatContext object and stream index
    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsNumber())
    {
        Napi::TypeError::New(info.Env(), "Object expected for argument 0: formatContext and Number expected for argument 1: streamIndex").ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    AVFormatContextObject *formatContextObject = Napi::ObjectWrap<AVFormatContextObject>::Unwrap(info[0].As<Napi::Object>());
    int streamIndex = info[1].As<Napi::Number>().Int32Value();

    AVStream *stream = formatContextObject->fmt_ctx_->streams[streamIndex];

    int ret = avcodec_parameters_copy(bsfContext->par_in, stream->codecpar);
    if (ret < 0)
    {
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    ret = av_bsf_init(bsfContext);
    if (ret < 0)
    {
        Napi::Error::New(info.Env(), AVErrorString(ret)).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }

    return info.Env().Undefined();
}
