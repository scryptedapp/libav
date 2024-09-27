import fs from 'fs';
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
                filter: 'hwdownload,format=nv12,hwupload,program_opencl=kernel=blur,hwdownload,format=nv12,scale,format=yuvj420p',
                hardwareDevice: 'opencl',
                frames: [
                    {
                        frame: frame,
                        timeBase: video,
                    }
                ],
            });

            openclFilter.sendCommand("program_opencl", "source", `
                __kernel void blur(__write_only image2d_t output_image, unsigned int index,
                        __read_only image2d_t input_image) {
                    int x = get_global_id(0);
                    int y = get_global_id(1);
                    int width = get_image_width(input_image);
                    int height = get_image_height(input_image);

                    int2 coords = (int2)(x, y);

                    if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                        uint4 src = read_imageui(input_image, coords);
                        write_imageui(output_image, coords, src);
                        return;
                    }

                    uint4 tl = read_imageui(input_image, coords + (int2)(-1, -1));
                    uint4 t = read_imageui(input_image, coords + (int2)(0, -1));
                    uint4 tr = read_imageui(input_image, coords + (int2)(1, -1));
                    uint4 l = read_imageui(input_image, coords + (int2)(-1, 0));
                    uint4 c = read_imageui(input_image, coords);
                    uint4 r = read_imageui(input_image, coords + (int2)(1, 0));
                    uint4 bl = read_imageui(input_image, coords + (int2)(-1, 1));
                    uint4 b = read_imageui(input_image, coords + (int2)(0, 1));
                    uint4 br = read_imageui(input_image, coords + (int2)(1, 1));

                    uint4 sumCorners = tl + tr + bl + br;
                    uint4 sixteenthCorners = sumCorners / 16;
                    uint4 sumSides = t + l + r + b;
                    uint4 eighthSides = sumSides / 8;
                    uint4 result = c / 4 + sixteenthCorners + eighthSides;

                    write_imageui(output_image, coords, result);
                }
            `)
        }

        openclFilter.addFrame(frame);
        using clframe = openclFilter.getFrame();
        console.log(clframe.pixelFormat);

        // const jpeg = clframe.toJpeg(1);
        // // save it
        // fs.writeFileSync('/tmp/output.jpg', jpeg);
    }
}

main();
