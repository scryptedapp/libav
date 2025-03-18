import { AVCodecContext, createAVBitstreamFilter, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('trace');

    const bsf = createAVBitstreamFilter('dump_extra');

    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:54559/0745382c566400e0");
    const video = readContext.streams.find(s => s.type === 'video')!;

    bsf.copyParameters(readContext, video.index);

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

        if (!encoder) {
            encoder = frame.createEncoder({
                encoder: 'h264_videotoolbox',
                bitrate: 2000000,
                timeBase: video,
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
