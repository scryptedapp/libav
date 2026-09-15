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
        else {
            if (formatContextObject->fmt_ctx_->nb_streams)
                av_write_trailer(formatContextObject->fmt_ctx_);
            AVIOContext *pb = formatContextObject->fmt_ctx_->pb;
            if (pb) {
                formatContextObject->fmt_ctx_->pb = nullptr;
                av_freep(&pb->buffer);
                avio_context_free(&pb);
            }
            avformat_free_context(formatContextObject->fmt_ctx_);
            formatContextObject->fmt_ctx_ = nullptr;
        }
        if (formatContextObject->callbackRef) {
            formatContextObject->callbackRef.Release();
        }
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
