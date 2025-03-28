#include "close-worker.h"
#include "../formatcontext.h"
#include "../error.h"

CloseWorker::CloseWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject)
{
}

void CloseWorker::Execute()
{
    if (formatContextObject->fmt_ctx_) {
        if (formatContextObject->is_input) {
            avformat_close_input(&formatContextObject->fmt_ctx_);
        }
        if (formatContextObject->callbackRef) {
            formatContextObject->callbackRef.Release();
        }
        avformat_free_context(formatContextObject->fmt_ctx_);
        formatContextObject->fmt_ctx_ = nullptr;
    }
}

void CloseWorker::OnOK()
{
    napi_resolve_deferred(Env(), deferred, Env().Undefined());
}

void CloseWorker::OnError(const Napi::Error &e)
{
    napi_reject_deferred(Env(), deferred, e.Value());
}
