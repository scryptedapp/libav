import fs from 'fs';
import { setAVLogLevel, createAVFormatContext, AVCodecContext } from '../src';

async function main() {
    setAVLogLevel('verbose');
    const ctx = createAVFormatContext();

    ctx.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const decoder = ctx.createDecoder('videotoolbox');
    // let encoder: AVCodecContext|undefined;
    console.log('opened', ctx.metadata, decoder.hardwareDevice, decoder.pixelFormat, decoder.hardwarePixelFormat);

    const start = Date.now();
    let frameCounter = 0;

    while (true) {
        if (frameCounter % 20 === 0) {
            const elapsed = Date.now() - start;
            console.log('fps', frameCounter / (elapsed / 1000));
        }

        using packet = await ctx.readFrame();
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

        frameCounter++;
        // console.log('frame', frame.width, frame.height, frame.format);

        using filter = ctx.createFilter(frame.width, frame.height, 'hwdownload,format=nv12,scale,format=yuvj420p', decoder);
        using softwareFrame = filter.filter(frame);

        // reusing the encoder seems to cause several quality loss after the first frame
        // if (!encoder) {
        //     encoder = softwareFrame.createEncoder({
        //         encoder: 'mjpeg',
        //         bitrate: 2000000,
        //         timeBaseNum: ctx.timeBaseNum,
        //         timeBaseDen: ctx.timeBaseDen,
        //         opts: {
        //             quality: 1,
        //             qmin: 1,
        //             qmax: 1,
        //         }
        //     });
        // }

        using encoder = softwareFrame.createEncoder({
            encoder: 'mjpeg',
            bitrate: 2000000,
            timeBaseNum: ctx.timeBaseNum,
            timeBaseDen: ctx.timeBaseDen,
            opts: {
                quality: 1,
                qmin: 1,
                qmax: 1,
            }
        });

        const sent = await encoder.sendFrame(softwareFrame);
        if (!sent) {
            console.error('sendFrame failed');
            continue;
        }

        using jpegPacket = await encoder.receivePacket();
        if (!jpegPacket) {
            console.error('receivePacket failed');
            continue;
        }

        const jpeg = jpegPacket.getData();

        // const jpeg = softwareFrame.toJpeg(1);
        console.log('jpeg size', jpeg.length);
        // fs.writeFileSync('test.jpg', jpeg);
    }
}

main();
