import { AVCodecContext, AVCodecFlags, AVFilter, AVFrame, AVPacket, createAVBitstreamFilter, createAVFilter, createAVFormatContext, createSdp, setAVLogLevel } from '../src';

async function main() {
    let seenKeyFrame = false;
    setAVLogLevel('trace');

    let videoFilterGraph: AVFilter | undefined;
    let audioFilterGraph: AVFilter | undefined;

    await using readContext = createAVFormatContext();
    await readContext.open("rtsp://192.168.2.130:49341/82db61046d761b5c");
    const video = readContext.streams.find(s => s.type === 'video')!;
    const audio = readContext.streams.find(s => s.type === 'audio')!;

    await using writeContext = createAVFormatContext();

    const encoderFps = 10;

    writeContext.create('rtp', rtp => {
        console.log('video', rtp.length);
    });
    let videoWriteStream: number | undefined;

    await using audioWriteContext = createAVFormatContext();
    audioWriteContext.create('rtp', (a) => {
        // console.log('audio', a.length);
    });
    let audioWriteStream: number | undefined;

    const hwaccel = process.platform === 'darwin' ? 'videotoolbox' : 'cuda';
    const videoEncoderCodec = process.platform === 'darwin' ? 'h264_videotoolbox' : 'h264_nvenc';

    using videoDecoder = readContext.createDecoder(video.index, hwaccel);
    using audioDecoder = readContext.createDecoder(audio.index);
    let videoEncoder: AVCodecContext | undefined;
    let audioEncoder: AVCodecContext | undefined;

    let framesEncoded = 0;
    while (framesEncoded < 500) {
        using frameOrPacket = await readContext.receiveFrame([
            {
                streamIndex: video.index,
                decoder: videoDecoder,
                filter: videoFilterGraph,
                encoder: videoEncoder,
            },
            {
                streamIndex: audio.index,
                decoder: audioDecoder,
                filter: audioFilterGraph,
                encoder: audioEncoder,
            }
        ]);
        if (!frameOrPacket)
            continue;

        if (frameOrPacket.type === 'packet') {
            if (frameOrPacket.inputStreamIndex === audio.index) {
                if (audioWriteStream === undefined) {
                    audioWriteStream = audioWriteContext.newStream({
                        codecContext: audioEncoder,
                    })
                }
                audioWriteContext.writeFrame(audioWriteStream, frameOrPacket);
            }
            else if (frameOrPacket.inputStreamIndex === video.index) {
                framesEncoded++;
                if (videoWriteStream === undefined) {
                    videoWriteStream = writeContext.newStream({
                        codecContext: videoEncoder,
                    });
                    console.log(createSdp([writeContext, audioWriteContext]));
                }
                writeContext.writeFrame(videoWriteStream, frameOrPacket);
            }
            else {
                console.error('Unknown stream index in packet', frameOrPacket.inputStreamIndex);
            }
            continue;
        }

        if (frameOrPacket.streamIndex === audio.index) {
            const audioFrame = frameOrPacket;

            if (!audioFilterGraph) {
                audioFilterGraph = createAVFilter({
                    filter: `aresample=48000,asetnsamples=n=960:p=0,aformat=flt`,
                    frames: [{
                        frame: audioFrame,
                        timeBase: audio,
                    }],
                });

                // can add the frame and continue, the next loop through the receiveFrame pipeline
                // will read it automatically.
                audioFilterGraph.addFrame(audioFrame);
                continue;
            }

            if (!audioEncoder) {
                audioEncoder = audioFrame.createEncoder({
                    encoder: 'libopus',
                    timeBase: audio,
                    bitrate: 40000,
                    opts: {
                        application: 'lowdelay',
                    },
                });

                if (!await audioEncoder.sendFrame(audioFrame))
                    console.error('sendFrame failed, frame will be dropped?');
                continue;
            }

            console.warn('audio frame received after encoder was created, this should not happen!');
            continue;
        }

        const frame = frameOrPacket;

        if (!videoFilterGraph) {
            videoFilterGraph = createAVFilter({
                filter: `fps=${encoderFps},setpts=N*(${video.timeBaseDen} / ${encoderFps})`,
                frames: [{
                    frame,
                    timeBase: video,
                }],
            });

            videoFilterGraph.addFrame(frame);
            continue;
        }

        if (!videoEncoder) {
            videoEncoder = frame.createEncoder({
                encoder: videoEncoderCodec,
                bitrate: 1000000,
                minRate: 10000,
                maxRate: 2000000,
                timeBase: { timeBaseNum: 1, timeBaseDen: encoderFps },
                framerate: { timeBaseNum: encoderFps, timeBaseDen: 1 },
                // request 1 minute idr from encoder
                gopSize: 60 * encoderFps,
                keyIntMin: 60 * encoderFps,
                opts: {
                    // needed by cuda and maybe others?
                    'forced-idr': 1,
                    // videotoolbox flag
                    realtime: 1,
                },
                flags: AVCodecFlags.AV_CODEC_FLAG_LOW_DELAY,
            });

            const sent = await videoEncoder.sendFrame(frame);
            if (!sent)
                console.error('sendFrame failed, frame will be dropped?');
            continue;
        }

        console.warn('video frame received after encoder was created, this should not happen!');
    }

    await new Promise(r => setTimeout(r, 1000));
    videoEncoder?.destroy();
    audioEncoder?.destroy();
}

main();
