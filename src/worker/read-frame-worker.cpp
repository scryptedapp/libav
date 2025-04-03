#include "read-frame-worker.h"
#include "../error.h"
#include "../packet.h"
#include "../frame.h"

ReadFrameWorker::ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, AVCodecContextObject *codecContextObject, int streamIndex)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject), codecContextObject(codecContextObject), streamIndex(streamIndex), packetResult(nullptr), frameResult(nullptr)
{
}

void ReadFrameWorker::Execute()
{
    frameResult = nullptr;
    packetResult = nullptr;

    AVFormatContext *fmt_ctx_ = formatContextObject->fmt_ctx_;
    if (!fmt_ctx_)
    {
        SetError("Format context is null");
        return;
    }

    AVCodecContext *codecContext = nullptr;
    if (codecContextObject)
    {
        codecContext = codecContextObject->codecContext;
        if (!codecContext)
        {
            SetError("Codec context is null");
            return;
        }
    }

    int ret;
    while (true)
    {
        if (codecContext)
        {
            AVFrame *frame = av_frame_alloc();
            if (!frame)
            {
                SetError("Failed to allocate frame");
                return;
            }

            // attempt to read frames first, EAGAIN will be returned if data needs to be
            // sent to decoder.
            ret = avcodec_receive_frame(codecContext, frame);
            if (!ret)
            {
                frameResult = frame;
                return;
            }

            av_frame_free(&frame);
            if (ret != AVERROR(EAGAIN))
            {
                SetError(AVErrorString(ret));
                return;
            }
        }

        while (true)
        {
            AVPacket *packet = av_packet_alloc();

            if (!packet)
            {
                SetError("Failed to allocate packet");
                return;
            }

            ret = av_read_frame(fmt_ctx_, packet);
            if (ret)
            {
                av_packet_free(&packet);
                // EAGAIN can try reading again later
                if (ret != AVERROR(EAGAIN))
                    SetError(AVErrorString(ret));
                return;
            }

            if (!codecContext)
            {
                packetResult = packet;
                return;
            }

            if (packet->stream_index != streamIndex)
            {
                // wrong stream, return the packet
                packetResult = packet;
                return;
            }

            ret = avcodec_send_packet(codecContext, packet);
            av_packet_free(&packet);

            // on decoder feed error, try again next packet.
            // could be starting on a non keyframe or data corruption
            // which may be recoverable.
            if (ret)
            {
                return;
            }

            // successful decode, try reading frame.
            break;
        }

        // loop and try reading frame again
    }
}

void ReadFrameWorker::OnOK()
{
    Napi::Env env = Env();
    if (packetResult)
    {
        napi_resolve_deferred(Env(), deferred, AVPacketObject::NewInstance(env, packetResult));
    }
    else if (frameResult)
    {
        napi_resolve_deferred(Env(), deferred, AVFrameObject::NewInstance(env, frameResult));
    }
    else
    {
        napi_resolve_deferred(Env(), deferred, env.Undefined());
    }
}

void ReadFrameWorker::OnError(const Napi::Error &e)
{
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
