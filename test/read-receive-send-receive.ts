import { AVCodecContext, AVFilter, createAVBitstreamFilter, createAVFilter, createAVFormatContext, createSdp, setAVLogLevel } from '../src';

async function main() {
    let seenKeyFrame = false;
    setAVLogLevel('trace');

    let filterGraph: AVFilter | undefined;

    using bsf = createAVBitstreamFilter('h264_mp4toannexb');

    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:54559/0745382c566400e0");
    const video = readContext.streams.find(s => s.type === 'video')!;
    const audio = readContext.streams.find(s => s.type === 'audio')!;

    bsf.copyParameters(readContext, video.index);

    using writeContext = createAVFormatContext();
    let frames = 0;
    let framesSinceIdr: number | undefined;
    const start = Date.now();

    let lastIdr = start;
    const encoderFps = 10;

    writeContext.create('rtp', (a) => {
        frames++;
        let naluType = a[12] & 0x1F;
        if (naluType === 28) {
            naluType = a[13] & 0x1F;
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
    const audioWriteStream = audioWriteContext.newStream({
        formatContext: readContext,
        streamIndex: readContext.streams.find(s => s.type === 'audio')?.index,
    })

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');
    let encoder: AVCodecContext | undefined;

    while (true) {
        using packet = await readContext.readFrame();
        if (!packet)
            continue;

        if (packet.streamIndex === audio.index) {
            audioWriteContext.writeFrame(audioWriteStream, packet);
            continue;
        }

        if (packet.streamIndex !== video.index)
            continue;

        try {
            await decoder.sendPacket(packet);
        }
        catch (e) {
            console.error('sendPacket error (recoverable)', e);
            continue;
        }

        using frame = await decoder.receiveFrame();
        if (!frame)
            continue;

        if (!filterGraph) {
            filterGraph = createAVFilter({
                filter: `fps=${encoderFps}`,
                frames: [{
                    frame,
                    timeBase: video,
                }],
            });
        }

        filterGraph.addFrame(frame);
        using filtered = filterGraph.getFrame();
        if (!filtered)
            continue;

        if (!encoder) {
            encoder = frame.createEncoder({
                encoder: 'h264_videotoolbox',
                bitrate: 2000000,
                timeBase: video,
                // request 1 minute idr from encoder
                gopSize: 60 * encoderFps,
                keyIntMin: 60 * encoderFps,
            });

            writeStream = writeContext.newStream({
                codecContext: encoder,
            });

            console.log(createSdp([writeContext, audioWriteContext]));
        }

        // this is necessary to prevent on demand keyframes
        // and use the gop size specified above.
        if (seenKeyFrame)
            frame.pictType = 2;
        else
            seenKeyFrame ||= frame.pictType === 1;

        // manually send 4 second idr
        if (Date.now() - lastIdr > 4000) {
            lastIdr = Date.now();
            frame.pictType = 1;
        }

        const sent = await encoder.sendFrame(frame);
        if (!sent) {
            console.error('sendFrame failed, frame will be dropped?');
            continue;
        }

        using transcodePacket = await encoder.receivePacket();
        if (!transcodePacket) {
            console.error('receivePacket needs more frames');
            continue;
        }

        bsf.sendPacket(transcodePacket);
        while (true) {
            using filtered = bsf.receivePacket();
            if (!filtered)
                break

            writeContext.writeFrame(writeStream!, filtered);
        }
    }
}

main();
