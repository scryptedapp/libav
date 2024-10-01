import fs from 'fs';
import { AVFilter, AVFrame, createAVFilter, createAVFormatContext, createAVFrame, setAVLogLevel, toBuffer, toJpeg } from '../src';

const resizeHalfKernel = `
    __kernel void resize_half(__write_only image2d_t output_image, int index, __read_only image2d_t input_image) {
        int x = get_global_id(0);
        int y = get_global_id(1);

        int2 outputCoords = (int2)(x, y);
        int2 inputCoords = outputCoords * 2;

        const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

        float4 result = read_imagef(input_image, sampler, inputCoords);

        write_imagef(output_image, outputCoords, result);
    }
`;

const blurKernel = `
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
`;

async function main() {
    setAVLogLevel('verbose');

    using readContext = createAVFormatContext();
    readContext.open("rtsp://scrypted-nvr:50757/68c1f365ed3e15b4");
    const video = readContext.streams.find(s => s.type === 'video')!;

    using decoder = readContext.createDecoder(video.index, 'videotoolbox');

    let blurFilter: AVFilter | undefined;
    let blurDiffFilter: AVFilter | undefined;

    let blurredFrame: AVFrame | undefined;

    let resizeFilter: AVFilter | undefined;
    let resizeCount = -1;

    while (true) {
        using frame = await readContext.receiveFrame(video.index, decoder);
        if (!frame)
            continue;

        if (!blurFilter) {
            blurFilter = createAVFilter({
                filter: 'hwmap,program_opencl=kernel=blur',
                hardwareDevice: 'opencl',
                frames: [
                    {
                        frame: frame,
                        timeBase: video,
                    }
                ],
            });

            blurFilter.sendCommand("program_opencl", "source", blurKernel);
        }

        if (!blurredFrame) {
            blurFilter.addFrame(frame);
            blurredFrame = blurFilter.getFrame();
            continue;
        }

        blurFilter.addFrame(frame);
        using newFrame = blurFilter.getFrame();

        if (!blurDiffFilter) {
            blurDiffFilter = createAVFilter({
                filter: '[in0][in1]program_opencl=kernel=diffnv12:format=gray:planar=0:inputs=2',
                frames: [
                    {
                        frame: blurredFrame,
                        timeBase: video,
                    },
                    {
                        frame: newFrame,
                        timeBase: video,
                    },
                ],
            });

            blurDiffFilter.sendCommand("program_opencl", "source", `
                    __kernel void diffnv12(__write_only image2d_t output_image, 
                        __read_only image2d_t y0_image, __read_only image2d_t uv0_image,
                        __read_only image2d_t y1_image, __read_only image2d_t uv1_image
                    ) {
                        int x = get_global_id(0);
                        int y = get_global_id(1);

                        int2 coords = (int2)(x, y);
                        int2 uvcoords = (int2)(x / 2, y / 2);

                        float4 y0 = read_imagef(y0_image, coords);
                        float4 y1 = read_imagef(y1_image, coords);
                        float4 uv0 = read_imagef(uv0_image, uvcoords);
                        float4 uv1 = read_imagef(uv1_image, uvcoords);

                        // printf("OCL: %d %d\\n", y0.x, y1.x);

                        float dy = fabs(y0.x - y1.x);
                        float du = fabs(uv0.x - uv1.x);
                        float dv = fabs(uv0.y - uv1.y);

                        float sy = step(0.12, dy);
                        float su = step(0.05, du);
                        float sv = step(0.05, dv);

                        float s = sy || su || sv;

                        float4 result;
                        result.x = s;
                        result.y = s;
                        result.z = s;
                        result.w = 1;

                        write_imagef(output_image, coords, result);
                    }
                `);
        }

        blurredFrame.pts = newFrame.pts;
        blurredFrame.dts = newFrame.dts;

        blurDiffFilter!.addFrame(blurredFrame, 0);
        blurDiffFilter!.addFrame(newFrame, 1);

        using diffedFrame = blurDiffFilter!.getFrame(0);
        console.log(diffedFrame.pixelFormat);

        // const jpeg  = await toJpeg(diffedFrame, 1);
        // fs.writeFileSync('/tmp/test.jpg', jpeg);

        if (!resizeFilter) {
            let currentWidth = diffedFrame.width;
            let currentHeight = diffedFrame.height;

            let filterStrings: string[] = [];

            while (currentWidth > 64 && currentHeight > 64) {
                const newWidth = Math.ceil(currentWidth / 2);
                const newHeight = Math.ceil(currentHeight / 2);

                filterStrings.push(`program_opencl@${resizeCount + 1}=kernel=resize_half:size=${newWidth}x${newHeight}`);

                currentWidth = newWidth;
                currentHeight = newHeight;
                resizeCount++;
            }

            const filterString = filterStrings.join(',');

            resizeFilter = createAVFilter({
                filter: filterString,
                frames: [
                    {
                        frame: diffedFrame,
                        timeBase: video,
                    }
                ],
            });

            for (let i = 0; i < resizeCount + 1; i++) {
                resizeFilter.sendCommand(`program_opencl@${i}`, "source", resizeHalfKernel);
            }
        }

        resizeFilter.addFrame(diffedFrame);
        using resizedFrame = resizeFilter.getFrame();
        const data = await toJpeg(resizedFrame, 1);
        fs.writeFileSync('/tmp/output.jpg', data);
    }
}

main();
