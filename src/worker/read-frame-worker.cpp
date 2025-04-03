#include "read-frame-worker.h"
#include "../error.h"
#include "../packet.h"
#include "../frame.h"

ReadFrameWorker::ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject,
                                 const std::map<int, AVCodecContextObject *> &decoders,
                                 const std::map<int, AVFilterGraphObject *> &filters)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject),
      decoders(decoders), filters(filters), packetResult(nullptr), frameResult(nullptr)
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

    // Allocate a single frame and packet to be reused
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (!packet || !frame)
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        SetError("Failed to allocate frame or packet");
        return;
    }

    int ret;
    while (true)
    {
        // Try to receive frames from each codec context first
        for (const auto &pair : decoders)
        {
            AVCodecContext *codecContext = pair.second->codecContext;
            if (!codecContext)
                continue;

            ret = avcodec_receive_frame(codecContext, frame);
            if (!ret)
            {
                // Check if there's a filter for this stream
                auto filterIt = filters.find(pair.first);
                if (filterIt == filters.end())
                {
                    // No filter, use decoded frame directly
                    av_packet_free(&packet);
                    frameResult = frame;
                    frameStreamIndex = pair.first;
                    return;
                }
                
                AVFilterGraphObject *filter = filterIt->second;
                AVFilterContext *buffersrc_ctx = filter->buffersrc_ctxs[0];
                AVFilterContext *buffersink_ctx = filter->buffersink_ctxs[0];

                // Feed the frame to the buffer source filter
                int ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
                if (ret < 0)
                {
                    av_frame_free(&frame);
                    av_packet_free(&packet);
                    SetError(AVErrorString(ret));
                    return;
                }

                // Get filtered frame
                AVFrame *filtered_frame = av_frame_alloc();
                ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame);
                if (ret == AVERROR(EAGAIN))
                {
                    // Need more input frames
                    av_frame_free(&filtered_frame);
                    continue;
                }
                if (ret < 0)
                {
                    av_frame_free(&frame);
                    av_frame_free(&filtered_frame);
                    av_packet_free(&packet);
                    SetError(AVErrorString(ret));
                    return;
                }

                // Use filtered frame
                av_frame_free(&frame);
                av_packet_free(&packet);
                frameResult = filtered_frame;
                frameStreamIndex = pair.first;
                return;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                av_frame_free(&frame);
                av_packet_free(&packet);
                SetError(AVErrorString(ret));
                return;
            }
        }

        // Need more data, try to read a packet
        ret = av_read_frame(fmt_ctx_, packet);
        if (ret == AVERROR(EAGAIN))
        {
            // try reading again later
            av_frame_free(&frame);
            av_packet_free(&packet);
            return;
        }
        else if (ret)
        {
            av_frame_free(&frame);
            av_packet_free(&packet);
            SetError(AVErrorString(ret));
            return;
        }

        // Check if we have a decoder for this stream
        auto it = decoders.find(packet->stream_index);
        if (it == decoders.end())
        {
            // No decoder for this stream, return the packet as-is
            av_frame_free(&frame);
            packetResult = packet;
            return;
        }

        // Send packet to appropriate decoder
        ret = avcodec_send_packet(it->second->codecContext, packet);
        av_packet_unref(packet);

        if (ret)
        {
            // On decoder feed error, try again with next packet
            av_frame_free(&frame);
            av_packet_free(&packet);
            return;
        }

        // Packet sent successfully, loop to try receiving frames
    }
}

void ReadFrameWorker::OnOK()
{
    Napi::Env env = Env();
    if (packetResult)
    {
        Napi::Object result = AVPacketObject::NewInstance(env, packetResult);
        result.Set("type", "packet");
        napi_resolve_deferred(Env(), deferred, result);
    }
    else if (frameResult)
    {
        Napi::Object result = AVFrameObject::NewInstance(env, frameResult);
        result.Set("streamIndex", frameStreamIndex);
        result.Set("type", "frame");
        napi_resolve_deferred(Env(), deferred, result);
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
