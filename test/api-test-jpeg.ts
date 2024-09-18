import { createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using ctx = createAVFormatContext();
    ctx.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = ctx.streams.find(s => s.type === 'video')!;
    using decoder = ctx.createDecoder(video.index, 'videotoolbox');

    while (true) {
        using packet = await ctx.readFrame();
        if (!packet)
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

        using filter = frame.createFilter({
            filter: 'hwdownload,format=nv12,scale,format=yuvj420p',
            timeBaseNum: video.timeBaseNum,
            timeBaseDen: video.timeBaseDen,
            codecContext: decoder,
        });
        using softwareFrame = filter.filter(frame);

        // reusing a jpeg encoder seems to cause several quality loss after the first frame
        using encoder = softwareFrame.createEncoder({
            encoder: 'mjpeg',
            bitrate: 2000000,
            timeBaseNum: video.timeBaseNum,
            timeBaseDen: video.timeBaseDen,
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

        using transcodePacket = await encoder.receivePacket();
        if (!transcodePacket) {
            console.error('receivePacket needs more frames');
            continue;
        }

        console.log('packet size', transcodePacket.size);
    }
}

main();
