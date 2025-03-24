#include "read-frame-worker.h"

ReadFrameWorker::ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject, AVCodecContextObject *codecContextObject, int streamIndex)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject), codecContextObject(codecContextObject), streamIndex(streamIndex), packetResult(nullptr), frameResult(nullptr)
{
}

void ReadFrameWorker::Execute() {
    frameResult = nullptr;
    packetResult = nullptr;

    AVFormatContext *fmt_ctx_ = formatContextObject->fmt_ctx_;
    if (!fmt_ctx_) {
        SetError("Format context is null");
        return;
    }

    AVCodecContext *codecContext = nullptr;
    AVFrame *frame;
    if (codecContextObject) {
        codecContext = codecContextObject->codecContext;
        if (!codecContext) {
            SetError("Codec context is null");
            return;
        }
        frame = av_frame_alloc();
    }

    AVPacket *packet = av_packet_alloc();

    if (!packet) {
        SetError("Failed to allocate packet");
        return;
    }

    int ret;
    while (true) {
        if (codecContext) {
            // attempt to read frames first, EAGAIN will be returned if data needs to be
            // sent to decoder.
            ret = avcodec_receive_frame(codecContext, frame);
            if (!ret) {
                av_packet_free(&packet);
                frameResult = frame;
                return;
            }
            else if (ret != AVERROR(EAGAIN)) {
                av_packet_free(&packet);
                SetError(AVErrorString(ret));
                return;
            }
        }

        while (true) {
            ret = av_read_frame(fmt_ctx_, packet);
            if (ret == AVERROR(EAGAIN)) {
                // try reading again later
                av_packet_free(&packet);
                return;
            }
            else if (ret) {
                av_packet_free(&packet);
                SetError(AVErrorString(ret));
                return;
            }

            if (!codecContext) {
                packetResult = packet;
                return;
            }

            if (packet->stream_index != streamIndex) {
                // wrong stream, return the packet
                packetResult = packet;
                return;
            }

            ret = avcodec_send_packet(codecContext, packet);
            av_packet_unref(packet);

            // on decoder feed error, try again next packet.
            // could be starting on a non keyframe or data corruption
            // which may be recoverable.
            if (ret) {
                av_packet_free(&packet);
                return;
            }

            // successful decode, try reading frame.
            break;
        }

        // loop and try reading frame again
    }
}

void ReadFrameWorker::OnOK() {
    Napi::Env env = Env();
    if (packetResult) {
        napi_resolve_deferred(Env(), deferred, AVPacketObject::NewInstance(env, packetResult));
    }
    else if (frameResult) {
        napi_resolve_deferred(Env(), deferred, AVFrameObject::NewInstance(env, frameResult));
    }
    else {
        napi_resolve_deferred(Env(), deferred, env.Undefined());
    }
}

void ReadFrameWorker::OnError(const Napi::Error &e) {
    napi_value error = e.Value();
    napi_reject_deferred(Env(), deferred, error);
}
