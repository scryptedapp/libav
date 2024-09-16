import { setAVLogLevel, createAVFormatContext } from '../src';

async function main() {
    setAVLogLevel('verbose');
    const ctx = createAVFormatContext();

    ctx.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const decoder = ctx.createDecoder(["videotoolbox"]);
    console.log('opened', ctx.metadata, decoder.hardwareDevice);

    while (true) {
        const packet = await ctx.readFrame();
        if (packet) {
            console.log('isKeyFrame', packet.isKeyFrame);
            packet.destroy();
            break;
        }
    }
}

main();
