import { AVCodecContext, AVFilter, AVFrame, AVPacket, createAVBitstreamFilter, createAVFilter, createAVFormatContext, createSdp, setAVLogLevel } from '../src';

async function main() {
    let seenKeyFrame = false;
    setAVLogLevel('trace');

    let videoFilterGraph: AVFilter | undefined;
    let audioFilterGraph: AVFilter | undefined;

    using bsf = createAVBitstreamFilter('h264_mp4toannexb');

    using readContext = createAVFormatContext();
    await readContext.open("rtsp://scrypted-nvr:53043/c3fadcb6b9a1d9b0");
    const video = readContext.streams.find(s => s.type === 'video')!;
    const audio = readContext.streams.find(s => s.type === 'audio')!;

    using writeContext = createAVFormatContext();
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

    using audioWriteContext = createAVFormatContext();
    audioWriteContext.create('rtp', (a) => {
    });
    let audioWriteStream: number|undefined;

    using videoDecoder = readContext.createDecoder(video.index, 'videotoolbox');
    using audioDecoder = readContext.createDecoder(audio.index);
    let videoEncoder: AVCodecContext | undefined;
    let audioEncoder: AVCodecContext | undefined;

    let framesEncoded = 0;
    while (framesEncoded < 50) {
        using frameOrPacket = await readContext.receiveFrame(video.index, videoDecoder);
        if (!frameOrPacket)
            continue;

        const packet = frameOrPacket as AVPacket;
        if (packet.streamIndex !== undefined) {
            if (packet.streamIndex === audio.index) {
                // audioWriteContext.writeFrame(audioWriteStream, packet);
                if (!await audioDecoder.sendPacket(packet)) {
                    console.error('sendPacket failed, frame will be dropped?');
                    continue;
                }
                using audioFrame = await audioDecoder.receiveFrame();
                if (!audioFrame)
                    continue;

                if (!audioFilterGraph) {
                    audioFilterGraph = createAVFilter({
                        filter: `aresample,format=s16`,
                        frames: [{
                            frame: audioFrame,
                            timeBase: audio,
                        }],
                    });
                }

                audioFilterGraph.addFrame(audioFrame);
                using resampledFrame = audioFilterGraph.getFrame();
                if (!resampledFrame)
                    continue;

                if (!audioEncoder) {
                    audioEncoder = resampledFrame.createEncoder({
                        encoder: 'libopus',
                        timeBase: audio,
                        bitrate: 40000,
                        channels: 2,
                        sampleRate: 48000,
                        opts: {
                            application: 'lowdelay',
                        },
                    });
                }

                if (!await audioEncoder.sendFrame(resampledFrame)) {
                    console.error('sendFrame failed, frame will be dropped?');
                    continue;
                }

                using transcodePacket = await audioEncoder.receivePacket();
                if (!transcodePacket)
                    continue;


                if (audioWriteStream === undefined) {
                    audioWriteStream = audioWriteContext.newStream({
                        codecContext: audioEncoder,
                    })
                }
                audioWriteContext.writeFrame(audioWriteStream, transcodePacket);
            }
            continue;
        }

        const frame = frameOrPacket as AVFrame;

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
                encoder: 'h264_videotoolbox',
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
                }
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
