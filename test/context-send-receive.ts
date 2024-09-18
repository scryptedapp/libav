import { AVCodecContext, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using readContext = createAVFormatContext();
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

    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    using decoder = readContext.createDecoder('videotoolbox');
    let encoder: AVCodecContext | undefined;
    console.log('opened', readContext.metadata, decoder.hardwareDevice, decoder.pixelFormat, decoder.hardwarePixelFormat);

    while (true) {
        using frame = await readContext.receiveFrame(decoder);
        if (!frame)
            continue;

        if (!encoder) {
            encoder = frame.createEncoder({
                encoder: 'h264_videotoolbox',
                bitrate: 2000000,
                timeBaseNum: readContext.timeBaseNum,
                timeBaseDen: readContext.timeBaseDen,
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
