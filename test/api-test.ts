import { setAVLogLevel, createAVFormatContext } from '../src';

async function main() {
    setAVLogLevel('verbose');
    const ctx = createAVFormatContext();

    ctx.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const decoder = ctx.createDecoder('videotoolbox');
    console.log('opened', ctx.metadata, decoder.hardwareDevice);

    const start = Date.now();
    let frameCounter = 0;

    while (true) {
        if (frameCounter % 20 === 0) {
            const elapsed = Date.now() - start;
            console.log('fps', frameCounter / (elapsed / 1000));
        }

        const packet = await ctx.readFrame();
        if (packet) {
            // console.log('isKeyFrame', packet.isKeyFrame);

            try {
                await decoder.sendPacket(packet);
                const frame = await decoder.receiveFrame();
                if (frame) {
                    frameCounter++;
                    // console.log('frame', frame.width, frame.height, frame.format);
                    frame.close();
                }
            }
            catch (e) {
                console.error('sendPacket error (recoverable)', e);
            }
        }
    }
}

main();
