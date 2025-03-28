#pragma once
#include <napi.h>

class AVFormatContextObject;

class CloseWorker : public Napi::AsyncWorker
{
public:
    CloseWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
};
