import { AVCodecContext, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('trace');
    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = readContext.streams.find(s => s.type === 'video')!;

    using writeContext = createAVFormatContext();
    writeContext.create('rtp', (a) => {
        const naluType = a[12] & 0x1F;
        if (naluType === 28) {
            const fuaNaluType = a[13] & 0x1F;
            console.log('nalu fua', fuaNaluType);
        }
        else {
            console.log('nalu', naluType);
        }
    });
    let writeStream: number | undefined;

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');
    let encoder: AVCodecContext | undefined;

    while (true) {
        using packet = await readContext.readFrame();
        if (!packet)
            continue;

        if (packet.streamindex !== video.index)
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

        if (!encoder) {
            encoder = frame.createEncoder({
                encoder: 'h264_videotoolbox',
                bitrate: 2000000,
                timeBaseNum: video.timeBaseNum,
                timeBaseDen: video.timeBaseDen,
            });

            writeStream = writeContext.newStream({
                codecContext: encoder,
            });
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

        writeContext.writeFrame(writeStream!, transcodePacket);
    }
}

main();
