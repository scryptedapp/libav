import { AVCodecContext, AVCodecFlags, AVFilter, AVFrame, AVPacket, createAVBitstreamFilter, createAVFilter, createAVFormatContext, createSdp, setAVLogLevel } from '../src';

async function main() {
    let seenKeyFrame = false;
    setAVLogLevel('trace');

    let videoFilterGraph: AVFilter | undefined;
    let audioFilterGraph: AVFilter | undefined;

    using bsf = createAVBitstreamFilter('h264_mp4toannexb');

    await using readContext = createAVFormatContext();
    await readContext.open("rtsp://192.168.2.130:49341/82db61046d761b5c");
    const video = readContext.streams.find(s => s.type === 'video')!;
    const audio = readContext.streams.find(s => s.type === 'audio')!;

    await using writeContext = createAVFormatContext();
    let frames = 0;
    let framesSinceIdr: number | undefined;
    const start = Date.now();

    let lastIdr = start;
    const encoderFps = 10;

    let lastReport = Date.now();
    let sinceLastReport = 0;

    writeContext.create('rtp', rtp => {
        sinceLastReport += rtp.length;
        if (Date.now() - lastReport > 4000) {
            // compute bitrate
            const bitrate = sinceLastReport * 8 / 4;
            console.log('bitrate', bitrate);
            lastReport = Date.now();
            sinceLastReport = 0;
        }

        frames++;
        let naluType = rtp[12] & 0x1F;
        if (naluType === 28) {
            naluType = rtp[13] & 0x1F;
        }

        if (naluType === 5) {
            if (framesSinceIdr === undefined || framesSinceIdr) {
                const fps = frames / (Date.now() - start) * 1000;
                console.log('idr', framesSinceIdr, 'fps', fps)
                framesSinceIdr = 0;
            }
        }
        else {
            // this is technically wrong because the rtp packet may be a fragment.
            // works though.
            if (typeof framesSinceIdr === 'number')
                framesSinceIdr++;
        }
    });
    let writeStream: number | undefined;

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
    while (framesEncoded < 50) {
        using frameOrPacket = await readContext.receiveFrame([
            {
                streamIndex: video.index,
                decoder: videoDecoder,
            },
            {
                streamIndex: audio.index,
                decoder: audioDecoder,
                filter: audioFilterGraph,
            }
        ]);
        if (!frameOrPacket)
            continue;

        if (frameOrPacket.type === 'packet')
            continue;

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
            }

            if (!await audioEncoder.sendFrame(audioFrame)) {
                console.error('sendFrame failed, frame will be dropped?');
                break;
            }

            using transcodePacket = await audioEncoder.receivePacket();
            if (!transcodePacket)
                break;


            if (audioWriteStream === undefined) {
                audioWriteStream = audioWriteContext.newStream({
                    codecContext: audioEncoder,
                })
            }
            audioWriteContext.writeFrame(audioWriteStream, transcodePacket);
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
        }

        videoFilterGraph.addFrame(frame);
        using filtered = videoFilterGraph.getFrame();
        if (!filtered)
            continue;

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

            writeStream = writeContext.newStream({
                codecContext: videoEncoder,
            });

            bsf.copyParameters(writeContext, writeStream);

            console.log(createSdp([writeContext, audioWriteContext]));
        }

        // this is necessary to prevent on demand keyframes
        // and use the gop size specified above.
        if (seenKeyFrame)
            frame.pictType = 2;
        else
            seenKeyFrame ||= frame.pictType === 1;

        // manually send 4 second idr
        // if (Date.now() - lastIdr > 4000) {
        //     lastIdr = Date.now();
        //     frame.pictType = 1;
        // }

        const sent = await videoEncoder.sendFrame(frame);
        if (!sent) {
            console.error('sendFrame failed, frame will be dropped?');
            continue;
        }

        using transcodePacket = await videoEncoder.receivePacket();
        if (!transcodePacket) {
            console.error('receivePacket needs more frames');
            continue;
        }

        bsf.sendPacket(transcodePacket);
        while (true) {
            using filtered = bsf.receivePacket();
            if (!filtered)
                break;

            writeContext.writeFrame(writeStream!, filtered);
            framesEncoded++;
        }
    }

    await new Promise(r => setTimeout(r, 1000));
    videoEncoder?.destroy();
    audioEncoder?.destroy();
}

main();
