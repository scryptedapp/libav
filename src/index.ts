import detectLibc from 'detect-libc';
import { once } from 'events';
import { https } from 'follow-redirects';
import fs from 'fs';
import { IncomingMessage } from 'http';
import path from 'path';
import { x as tarx } from 'tar';
import packageJson from '../package.json';
import { AVPictureType } from './flags';
export * from './flags';
export * as Profile from './profiles';

let addon: any;

export function isLoaded() {
    return !!addon;
}

function getReleasePath(installPath: string) {
    return path.join(installPath, 'build/Release');
}

const packagePath = path.join(__dirname, '..');
export function getAddonPath(installPath = packagePath) {
    return path.join(getReleasePath(installPath), 'addon.node');
}

export function getFFmpegPath(installPath = packagePath) {
    return path.join(getReleasePath(installPath), process.platform === 'win32' ? 'ffmpeg.exe' : 'ffmpeg');
}

export function loadAddon(addonPath = getAddonPath(), nr: NodeRequire = require) {
    if (isLoaded())
        return addon;
    addon = nr(addonPath);
    return addon;
}

let installing: Promise<void> | undefined;
export async function install(installPath = packagePath, nr: NodeRequire | null = require) {
    const addonPath = getAddonPath(installPath);
    if (!nr) {
        if (fs.existsSync(addonPath))
            return;
    }
    else {
        if (isLoaded())
            return;

        if (installing)
            return installing;

        try {
            loadAddon(addonPath, nr);
            return;
        }
        catch (e) {
        }
    }

    installing = (async () => {
        await downloadAddon(installPath);
        if (!nr)
            return;
        loadAddon(addonPath, nr);
    })()
        .finally(() => {
            installing = undefined;
        });

    return installing;
}

export async function downloadAddon(installPath?: string) {
    const cwd = installPath || process.cwd();
    await fs.promises.mkdir(cwd, { recursive: true });
    const buildPath = path.join(cwd, 'build');
    const extractPath = path.join(cwd, '.extract');
    try {
        await fs.promises.rm(extractPath, { recursive: true, force: true });
    }
    catch (e) {
        const oldExtractPath = path.join(cwd, '.extract-old');
        fs.promises.rm(oldExtractPath, { recursive: true, force: true });
        await fs.promises.rename(extractPath, oldExtractPath);
    }
    await fs.promises.mkdir(extractPath, { recursive: true });
    await fs.promises.rm(buildPath, { recursive: true, force: true });
    const binaryUrl = getBinaryUrl();
    const r = https.get(binaryUrl, {
        family: 4,
    });
    const [response] = await once(r, 'response') as [IncomingMessage];
    if (!response.statusCode || (response.statusCode < 200 && response.statusCode >= 300))
        throw new Error(`Failed to download libav binary: ${response.statusCode}`);
    const t = response.pipe(tarx({
        cwd: extractPath,
    }));
    await once(t, 'end');
    await fs.promises.rename(path.join(extractPath, 'build'), buildPath);
}

export interface AVFrame extends AVTimeBase {
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: string;
    readonly hardwareDeviceType: string;
    readonly softwareFormat: string;
    pictType: AVPictureType;

    flags: number;
    timeBaseNum: number;
    timeBaseDen: number;
    pts: number;
    dts: number;
    format: number;
    sampleCount: number;

    [Symbol.dispose](): void;
    destroy(): void;
    toBuffer(): Buffer;
    fromBuffer(buffer: Buffer): void;
    createEncoder(options: {
        encoder: string,
        bitrate: number,
        minRate?: number;
        maxRate?: number,
        bufSize?: number,
        timeBase: AVTimeBase,
        framerate?: AVTimeBase,
        flags?: number,
        opts?: {
            [key: string]: string | number,
        },
        profile?: number;
        gopSize?: number;
        keyIntMin?: number;
    }): AVCodecContext;
}

export interface AVFilter {
    [Symbol.dispose](): void;
    destroy(): void;
    sendCommand(target: string, command: string, arg: string): void;
    addFrame(frame: AVFrame, index?: number): void;
    getFrame(index?: number): AVFrame;
}

export interface AVBitstreamFilter {
    [Symbol.dispose](): void;
    destroy(): void;
    setOption(name: string, value: string): void;
    sendPacket(packet: AVPacket): void;
    receivePacket(): AVPacket;
    copyParameters(formatContext: AVFormatContext, streamIndex: number): void;
}

export interface AVPacket {
    readonly streamIndex: number;
    flags: number;
    readonly size: number;
    pts: number;
    dts: number;
    readonly duration: number;

    [Symbol.dispose](): void;
    getData(): Buffer;
    destroy(): void;
}

export interface AVCodecContext extends AVTimeBase {
    readonly hardwareDevice: string;
    /**
     * The pixel format of the output frames.
     * May not be available until the first frame is read.
     */
    readonly pixelFormat: string;
    readonly hardwarePixelFormat: string;
    readonly vendorInfo: Record<string, any>;
    keyIntMin: number;
    gopSize: number;

    [Symbol.dispose](): void;
    destroy(): void;
    sendPacket(packet: AVPacket): Promise<boolean>;
    receiveFrame(): Promise<AVFrame>;
    sendFrame(packet: AVFrame): Promise<boolean>;
    receivePacket(): Promise<AVPacket>;
}

export interface AVStream extends AVTimeBase {
    readonly index: number;
    readonly codec: string;
    readonly type: string;
    readonly width: number;
    readonly height: number;
}

export interface AVTimeBase {
    readonly timeBaseNum: number,
    readonly timeBaseDen: number,
}

export interface AVFormatContext {
    readonly metadata: any;
    readonly streams: AVStream[];

    // dispose is async here because the format context may be using a network input
    // like RTSP which may require issuing and waiting for a TEARDOWN
    [Symbol.asyncDispose](): Promise<void>;
    open(input: string, options?: Record<string, string>): Promise<void>;
    createDecoder(streamIndex: number, hardwareDevice?: string, decoder?: string, deviceName?: string): AVCodecContext;
    readFrame(): Promise<AVPacket>;
    receiveFrame(pipelines: {
        streamIndex: number;
        decoder?: AVCodecContext;
        filter?: AVFilter;
        encoder?: AVCodecContext;
        writeFormatContext?: AVFormatContext;
    }[]): Promise<(AVFrame & { streamIndex: number; type: 'frame'; }) | (AVPacket & { type: 'packet'; inputStreamIndex?: number; }) | null | undefined>;
    create(format: string, callback: (buffer: Buffer) => void): void;
    newStream(options: {
        codecContext?: AVCodecContext
    } | {
        formatContext?: AVFormatContext,
        streamIndex?: number,
    } | {
        bsf: AVBitstreamFilter,
    }): number;
    writeFrame(streamIndex: number, packet: AVPacket): void;
    createSDP(): string;
    close(): Promise<void>;
}

export function setAVLogLevel(level: 'quiet' | 'panic' | 'fatal' | 'error' | 'warning' | 'info' | 'verbose' | 'debug' | 'trace') {
    loadAddon().setLogLevel(level);
}

export function createAVFormatContext(): AVFormatContext {
    return new (loadAddon().AVFormatContext)();
}

export function createAVFilter(options: {
    filter: string,
    frames: {
        frame: AVFrame,
        timeBase: AVTimeBase,
    }[],
    autoConvert?: boolean,
    threadCount?: number,
    hardwareDevice?: string,
    hardwareDeviceName?: string,
    hardwareDeviceFrame?: AVFrame;
    // outCount defaults to 1
    outCount?: number,
}): AVFilter {
    return new (loadAddon().AVFilter)(options);
}

export function createAVFrame(
    width: number,
    height: number,
    pixelFormat: string,
    fillBlack = true,
): AVFrame {
    return new (loadAddon().AVFrame)(width, height, pixelFormat, fillBlack);
}

export function createAVBitstreamFilter(filter: string): AVBitstreamFilter {
    return new (loadAddon().AVBitstreamFilter)(filter);
}

export function getBinaryUrl() {
    const libc = process.env.LIBC || process.env.npm_config_libc ||
        (detectLibc.isNonGlibcLinuxSync() && detectLibc.familySync()) || ''

    const { name, version } = packageJson;
    const binaryName = name.replace(/^@[a-zA-Z0-9_\-.~]+\//, '')

    const abi = process.versions.modules;
    const runtime = process.env.npm_config_runtime || 'node';
    const { platform, arch } = process;
    const packageName = `${binaryName}-v${version}-${runtime}-v${abi}-${platform}${libc}-${arch}.tar.gz`;

    const url = `https://github.com/scryptedapp/libav/releases/download/v${version}/${packageName}`;

    return url;
}

async function encodeJpeg(softwareFrame: AVFrame, quality: number) {
    using encoder = softwareFrame.createEncoder({
        encoder: 'mjpeg',
        bitrate: 2000000,
        timeBase: {
            timeBaseNum: 1,
            timeBaseDen: 25,
        },
        opts: {
            quality,
            qmin: quality,
            qmax: quality,
        },
    });

    const sent = await encoder.sendFrame(softwareFrame);
    if (!sent)
        throw new Error('sendFrame failed');

    using transcodePacket = await encoder.receivePacket();
    if (!transcodePacket)
        throw new Error('receivePacket needs more frames');

    return transcodePacket.getData();
}

export function createSdp(formatContexts: AVFormatContext[]): string {
    return loadAddon().createSdp(formatContexts);
}

export async function toJpeg(frame: AVFrame, quality: number) {
    if (frame.pixelFormat === 'yuvj420p')
        return await encodeJpeg(frame, quality);

    let filterString = `scale,format=yuvj420p`;
    if (frame.hardwareDeviceType)
        filterString = `hwdownload,format=${frame.softwareFormat},${filterString}`;

    using filter = createAVFilter({
        filter: filterString,
        frames: [
            {
                frame,
                timeBase: {
                    timeBaseNum: 1,
                    timeBaseDen: 25,
                },
            }
        ],
    });

    filter.addFrame(frame);
    using softwareFrame = filter.getFrame();
    softwareFrame.pts = 0;
    softwareFrame.dts = 0;

    return await encodeJpeg(softwareFrame, quality);
}

export async function toBuffer(frame: AVFrame) {
    if (!frame.hardwareDeviceType)
        return frame.toBuffer();

    const filterString = `hwdownload,format=${frame.softwareFormat}`;
    using filter = createAVFilter({
        filter: filterString,
        frames: [
            {
                frame,
                timeBase: {
                    timeBaseNum: 1,
                    timeBaseDen: 25,
                },
            }
        ],
    });

    filter.addFrame(frame);
    using softwareFrame = filter.getFrame();
    return softwareFrame.toBuffer();
}

export const version: string = packageJson.version;