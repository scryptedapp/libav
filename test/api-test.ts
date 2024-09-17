import { writeFileSync } from 'fs';
import { AVCodecContext, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using readContext = createAVFormatContext();
    using writeContext = createAVFormatContext();
    writeContext.create('rtp', (a) => {
        // console.log(a);
    });
    let writeStream: number| undefined;

    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    using decoder = readContext.createDecoder('videotoolbox');
    let encoder: AVCodecContext | undefined;
    console.log('opened', readContext.metadata, decoder.hardwareDevice, decoder.pixelFormat, decoder.hardwarePixelFormat);

    while (true) {
        using packet = await readContext.readFrame();
        if (!packet)
            continue;
        // console.log('isKeyFrame', packet.isKeyFrame);

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

        // using filter = frame.createFilter({
        //     filter: 'hwdownload,format=nv12',
        //     codecContext: decoder,
        //     timeBaseNum: ctx.timeBaseNum,
        //     timeBaseDen: ctx.timeBaseDen,
        // });

        // const softwareFrame = filter.filter(frame);
        // const data = softwareFrame.toBuffer();

        // reusing the jpeg encoder seems to cause several quality loss after the first frame
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

        // console.log('packet size', transcodePacket.size);
        writeContext.writeFrame(writeStream!, transcodePacket);
        // break;
    }
}

main();
