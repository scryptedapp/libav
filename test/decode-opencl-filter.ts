import fs from 'fs';
import { AVFilter, AVFrame, createAVFilter, createAVFormatContext, setAVLogLevel } from '../src';

async function main() {
    setAVLogLevel('verbose');
    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = readContext.streams.find(s => s.type === 'video')!;

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');

    let uploadFilter: AVFilter | undefined;
    let blurFilter: AVFilter | undefined;
    let blurDiffFilter: AVFilter | undefined;

    let blurredFrame: AVFrame | undefined;

    while (true) {
        using frame = await readContext.receiveFrame(video.index, decoder);
        if (!frame)
            continue;

        if (!blurFilter) {
            blurFilter = createAVFilter({
                filter: 'hwmap,program_opencl=kernel=blur,hwdownload,format=nv12',
                hardwareDevice: 'opencl',
                frames: [
                    {
                        frame: frame,
                        timeBase: video,
                    }
                ],
            });

            blurFilter.sendCommand("program_opencl", "source", `
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
            `);
        }

        if (!blurredFrame) {
            blurFilter.addFrame(frame);
            blurredFrame = blurFilter.getFrame();

            // uploadFilter = createAVFilter({
            //     filter: 'scale,format=nv12,hwupload',
            //     hardwareDevice: 'videotoolbox',
            //     frames: [
            //         {
            //             frame: blurredFrame,
            //             timeBase: video,
            //         }
            //     ],
            // });

            // uploadFilter.addFrame(blurredFrame);
            // blurredFrame = uploadFilter.getFrame();

            continue;
        }

        uploadFilter = createAVFilter({
            filter: 'hwdownload,format=nv12',
            frames: [
                {
                    frame: frame,
                    timeBase: video,
                }
            ],
        });

        uploadFilter.addFrame(frame);
        using derpFrame= uploadFilter.getFrame();

        if (!blurDiffFilter) {
            blurDiffFilter = createAVFilter({
                filter: '[in0]scale,format=nv12,hwupload[hw0];[in1]scale,format=nv12,hwupload[hw1];[hw0][hw1]program_opencl=kernel=blurDiff:inputs=2,hwdownload,format=nv12,scale,format=yuvj420p',
                hardwareDevice: 'opencl',
                frames: [
                    {
                        frame: blurredFrame,
                        timeBase: video,
                    },
                    {
                        frame: derpFrame,
                        timeBase: video,
                    }
                ],
            });

            blurDiffFilter.sendCommand("program_opencl", "source", `
                    __kernel void blurDiff(__write_only image2d_t output_image, unsigned int index,
                            __read_only image2d_t blurred_image, __read_only image2d_t input_image) {
                        int x = get_global_id(0);
                        int y = get_global_id(1);
                        int width = get_image_width(input_image);
                        int height = get_image_height(input_image);

                        int2 coords = (int2)(x, y);

                        uint4 blurred = read_imageui(blurred_image, coords);
                        uint4 input = read_imageui(input_image, coords);
                        uint4 diff = abs_diff(blurred, input);

                        write_imageui(output_image, coords, diff);
                    }
                `);
        }
        blurDiffFilter!.addFrame(blurredFrame, 1);
        blurDiffFilter!.addFrame(derpFrame, 0);

        const diffedFrame = blurDiffFilter!.getFrame();

        const jpeg = diffedFrame.toJpeg(1);
        // save it
        fs.writeFileSync('/tmp/output.jpg', jpeg);
    }
}

main();
