#include "read-frame-worker.h"
#include "../error.h"
#include "../packet.h"
#include "../frame.h"

ReadFrameWorker::ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject,
                                 const std::map<int, AVCodecContextObject *> &decoders,
                                 const std::map<int, AVFilterGraphObject *> &filters,
                                 const std::map<int, AVCodecContextObject *> &encoders)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject),
      decoders(decoders), filters(filters), encoders(encoders), packetResult(nullptr), frameResult(nullptr)
{
}

void ReadFrameWorker::Execute()
{
    frameResult = nullptr;
    packetResult = nullptr;
    packetInputStreamIndex = -1;

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
        // Try to receive encoded packets first
        for (const auto &pair : encoders)
        {
            AVCodecContext *encoderContext = pair.second->codecContext;
            if (!encoderContext)
                continue;

            ret = avcodec_receive_packet(encoderContext, packet);
            if (!ret)
            {
                // Got an encoded packet
                av_frame_free(&frame);
                packetResult = packet;
                packetInputStreamIndex = pair.first;
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

        // Try to receive filtered frames and feed to encoders
        for (const auto &pair : filters)
        {
            AVFilterGraphObject *filter = pair.second;
            AVFilterContext *buffersink_ctx = filter->buffersink_ctxs[0];

            AVFrame *filtered_frame = av_frame_alloc();
            ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame);

            if (!ret)
            {
                // Check for encoder
                auto encoderIt = encoders.find(pair.first);
                if (encoderIt == encoders.end())
                {
                    // No encoder, use filtered frame directly
                    av_frame_free(&frame);
                    av_packet_free(&packet);
                    frameResult = filtered_frame;
                    frameStreamIndex = pair.first;
                    return;
                }

                // Send filtered frame to encoder
                ret = avcodec_send_frame(encoderIt->second->codecContext, filtered_frame);
                av_frame_free(&filtered_frame);
                if (ret < 0)
                {
                    av_frame_free(&frame);
                    av_packet_free(&packet);
                    SetError(AVErrorString(ret));
                    return;
                }
                continue;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                av_frame_free(&filtered_frame);
                if (ret != AVERROR_EOF)
                {
                    av_frame_free(&frame);
                    av_packet_free(&packet);
                    SetError(AVErrorString(ret));
                    return;
                }
            }
            av_frame_free(&filtered_frame);
        }

        // Try to receive frames from each decoder context
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
                if (filterIt != filters.end())
                {
                    // Feed frame to filter and continue - filter output will be handled in filter loop
                    AVFilterContext *buffersrc_ctx = filterIt->second->buffersrc_ctxs[0];
                    ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
                    if (ret < 0)
                    {
                        av_frame_free(&frame);
                        av_packet_free(&packet);
                        SetError(AVErrorString(ret));
                        return;
                    }
                    continue;
                }

                // No filter, check for encoder
                auto encoderIt = encoders.find(pair.first);
                if (encoderIt == encoders.end())
                {
                    // No encoder, use decoded frame directly
                    av_packet_free(&packet);
                    frameResult = frame;
                    frameStreamIndex = pair.first;
                    return;
                }

                // Send frame to encoder
                ret = avcodec_send_frame(encoderIt->second->codecContext, frame);
                if (ret < 0)
                {
                    av_frame_free(&frame);
                    av_packet_free(&packet);
                    SetError(AVErrorString(ret));
                    return;
                }
                continue;
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
        if (packetInputStreamIndex >= 0)
            result.Set("inputStreamIndex", Napi::Number::New(env, packetInputStreamIndex));
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
