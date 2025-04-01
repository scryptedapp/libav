#include "open-worker.h"
#include "../formatcontext.h"
#include "../error.h"

OpenWorker::OpenWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, const std::string &filename, AVDictionary* options)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject), filename(filename), options(options)
{
}

void OpenWorker::Execute()
{
    int ret = avformat_open_input(&formatContextObject->fmt_ctx_, filename.c_str(), NULL, &options);
    if (ret < 0)
    {
        SetError(AVErrorString(ret));
        return;
    }
    if (options) {
        av_dict_free(&options);
    }
    formatContextObject->is_input = true;
}

void OpenWorker::OnOK()
{
    napi_resolve_deferred(Env(), deferred, Env().Undefined());
}

void OpenWorker::OnError(const Napi::Error &e)
{
    napi_reject_deferred(Env(), deferred, e.Value());
}
