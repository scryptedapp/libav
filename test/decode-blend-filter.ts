import { AVFilter, createAVFilter, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = readContext.streams.find(s => s.type === 'video')!;

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');

    let blendFilter: AVFilter | undefined;

    while (true) {
        using frame1 = await readContext.receiveFrame(video.index, decoder);
        using frame2 = await readContext.receiveFrame(video.index, decoder);

        if (!frame1 || !frame2)
            continue;

        if (!blendFilter) {
            blendFilter = createAVFilter({
                filter: '[in0]hwdownload,format=nv12,scale[f0];[in1]hwdownload,format=nv12,scale[f1];[f0][f1]blend=all_mode=overlay',
                frames: [
                    {
                        frame: frame1,
                        timeBase: video,
                    },
                    {
                        frame: frame2,
                        timeBase: video,
                    }
                ],
            });
        }

        blendFilter.addFrame(frame1, 0);
        blendFilter.addFrame(frame2, 1);
        using blendedFrame = blendFilter.getFrame();
        console.log(blendedFrame.pixelFormat);
    }
}

main();
