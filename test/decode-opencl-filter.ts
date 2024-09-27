import { AVFilter, createAVFilter, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = readContext.streams.find(s => s.type === 'video')!;

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');

    let openclFilter: AVFilter|undefined;

    while (true) {
        using frame = await readContext.receiveFrame(video.index, decoder);
        if (!frame)
            continue;

        if (!openclFilter) {
            openclFilter = createAVFilter({
                filter: 'hwdownload,format=nv12,hwupload,program_opencl=kernel=set_black',
                hardwareDevice: 'opencl',
                frames: [
                    {
                        frame: frame,
                        timeBase: video,
                    }
                ],
            });

            openclFilter.sendCommand("program_opencl", "source", `
                __kernel void set_black(__write_only image2d_t output_image, unsigned int index,
                        __read_only image2d_t input_image) {
                int x = get_global_id(0);
                int y = get_global_id(1);

                int2 coords = (int2)(x, y);

                // Set pixel to black with full opacity (RGBA = 0, 0, 0, 255)
                uint4 black_pixel = (uint4)(0, 0, 0, 255);

                // Write the black pixel to the output image
                write_imageui(output_image, coords, black_pixel);
                }
            `)
        }

        openclFilter.addFrame(frame);
        using clframe = openclFilter.getFrame();
        console.log(clframe.pixelFormat);
        const buffer = clframe.toJpeg(1);
    }
}

main();
