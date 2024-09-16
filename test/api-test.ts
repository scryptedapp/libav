import { setAVLogLevel, createAVFormatContext } from '../src';

async function main() {
    setAVLogLevel('verbose');
    const ctx = createAVFormatContext();

    ctx.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const decoder = ctx.createDecoder('videotoolbox');
    console.log('opened', ctx.metadata, decoder.hardwareDevice, decoder.pixelFormat);

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
        const jpeg = softwareFrame.toJpeg(1);
        console.log('jpeg size', jpeg.length);
    }
}

main();
