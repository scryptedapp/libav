#include "read-frame-worker.h"
#include "../error.h"
#include "../packet.h"
#include "../frame.h"
#include "../av-pointer.h"

ReadFrameWorker::ReadFrameWorker(napi_env env, napi_deferred deferred, AVFormatContextObject *formatContextObject,
                                 const std::map<int, AVCodecContextObject *> &decoders,
                                 const std::map<int, AVFilterGraphObject *> &filters,
                                 const std::map<int, AVCodecContextObject *> &encoders,
                                 const std::map<int, AVFormatContextObject *> &writeFormatContexts)
    : Napi::AsyncWorker(env), deferred(deferred), formatContextObject(formatContextObject),
      decoders(decoders), filters(filters), encoders(encoders), writeFormatContexts(writeFormatContexts),
      packetResult(nullptr), frameResult(nullptr)
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

    FreePointer<AVFrame, av_frame_free> frame(av_frame_alloc());
    FreePointer<AVPacket, av_packet_free> packet(av_packet_alloc());
    if (!packet.get() || !frame.get())
    {
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

            ret = avcodec_receive_packet(encoderContext, packet.get());
            if (!ret)
            {
                // Got an encoded packet
                auto it = writeFormatContexts.find(pair.first);
                if (it != writeFormatContexts.end())
                {
                    AVFormatContextObject *writeContext = it->second;
                    packet.get()->stream_index = 0;
                    ret = av_write_frame(writeContext->fmt_ctx_, packet.get());
                    if (ret < 0)
                    {
                        SetError(AVErrorString(ret));
                        return;
                    }
                    // writing a muxer doesn't have a result so keep
                    // going until something is available or demuxer needs
                    // more data.
                    continue;
                }

                packetResult = packet.release();
                packetInputStreamIndex = pair.first;
                return;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                SetError(AVErrorString(ret));
                return;
            }
        }

        // Try to receive filtered frames and feed to encoders
        for (const auto &pair : filters)
        {
            auto filter = pair.second;
            auto buffersink_ctx = filter->buffersink_ctxs[0];

            FreePointer<AVFrame, av_frame_free> filtered_frame(av_frame_alloc());
            ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame.get());

            if (!ret)
            {
                // Check for encoder
                auto encoderIt = encoders.find(pair.first);
                if (encoderIt == encoders.end())
                {
                    // No encoder, use filtered frame directly
                    frameResult = filtered_frame.release();
                    frameStreamIndex = pair.first;
                    return;
                }

                // Send filtered frame to encoder
                ret = avcodec_send_frame(encoderIt->second->codecContext, filtered_frame.get());
                if (ret < 0)
                {
                    SetError(AVErrorString(ret));
                    return;
                }
                continue;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                SetError(AVErrorString(ret));
                return;
            }
        }

        // Try to receive frames from each decoder context
        for (const auto &pair : decoders)
        {
            auto codecContext = pair.second->codecContext;
            if (!codecContext)
                continue;

            ret = avcodec_receive_frame(codecContext, frame.get());
            if (!ret)
            {
                // Check if there's a filter for this stream
                auto filterIt = filters.find(pair.first);
                if (filterIt != filters.end())
                {
                    // Feed frame to filter and continue - filter output will be handled in filter loop
                    AVFilterContext *buffersrc_ctx = filterIt->second->buffersrc_ctxs[0];
                    ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
                    if (ret < 0)
                    {
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
                    frameResult = frame.release();
                    frameStreamIndex = pair.first;
                    return;
                }

                // Send frame to encoder
                ret = avcodec_send_frame(encoderIt->second->codecContext, frame.get());
                if (ret < 0)
                {
                    SetError(AVErrorString(ret));
                    return;
                }
                continue;
            }
            else if (ret != AVERROR(EAGAIN))
            {
                SetError(AVErrorString(ret));
                return;
            }
        }

        // Need more data, try to read a packet
        ret = av_read_frame(fmt_ctx_, packet.get());
        if (ret == AVERROR(EAGAIN))
        {
            // try reading again later
            return;
        }
        else if (ret)
        {
            SetError(AVErrorString(ret));
            return;
        }

        // Check if we have a decoder for this stream
        auto it = decoders.find(packet.get()->stream_index);
        if (it == decoders.end())
        {
            auto it = writeFormatContexts.find(packet.get()->stream_index);
            if (it != writeFormatContexts.end())
            {
                auto writeContext = it->second;
                packetInputStreamIndex = packet.get()->stream_index;
                packet.get()->stream_index = 0;
                ret = av_write_frame(writeContext->fmt_ctx_, packet.get());
                if (ret < 0)
                {
                    SetError(AVErrorString(ret));
                    return;
                }
            }

            // No decoder for this stream, return the packet as-is
            packetResult = packet.release();
            return;
        }

        // Send packet to appropriate decoder
        ret = avcodec_send_packet(it->second->codecContext, packet.get());
        av_packet_unref(packet.get());

        if (ret)
        {
            // On decoder feed error, try again with next packet
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
