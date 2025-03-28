#pragma once
#include <napi.h>

class AVFormatContextObject;

class OpenWorker : public Napi::AsyncWorker
{
public:
    OpenWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, const std::string &filename);
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;

private:
    napi_deferred deferred;
    AVFormatContextObject *formatContextObject;
    std::string filename;
};
