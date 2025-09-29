import { createAVFilter, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    await using ctx = createAVFormatContext();
    await ctx.open("rtsp://scrypted-nvr:38999/cd527b59da41950b");
    const video = ctx.streams.find(s => s.type === 'video')!;
    using decoder = ctx.createDecoder(video.index, 'videotoolbox');

    while (true) {
        using packet = await ctx.readFrame();
        if (!packet || packet.streamIndex !== video.index)
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

        using filter = createAVFilter({
            filter: 'hwdownload,format=nv12,scale,format=yuvj420p',
            frames: [
                {
                    frame,
                    timeBase: video,
                }
            ],
        });
        filter.addFrame(frame);
        using softwareFrame = filter.getFrame();

        // reusing a jpeg encoder seems to cause several quality loss after the first frame
        using encoder = softwareFrame.createEncoder({
            encoder: 'mjpeg',
            bitrate: 2000000,
            timeBase: video,
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
