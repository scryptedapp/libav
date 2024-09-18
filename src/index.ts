import detectLibc from 'detect-libc';
import { once } from 'events';
import { https } from 'follow-redirects';
import fs from 'fs';
import path from 'path';
import { x as tarx } from 'tar';
import packageJson from '../package.json';

let addon: any;
try {
    addon = require('../build/Release/addon');
}
catch (e) {
}

export function isInstalled() {
    return !!addon;
}

export function loadAddon(addonPath: string, nr: NodeRequire = require) {
    addon = nr(addonPath);
}

let installing: Promise<void> | undefined;
export async function install(installPath?: string, nr: NodeRequire = require) {
    if (addon)
        return;

    if (installing)
        return installing;

    const addonPath = path.join(installPath || '', 'build/Release/addon');
    try {
        loadAddon(addonPath, nr);
        return;
    }
    catch (e) {
    }

    installing = (async () => {
        await downloadAddon(installPath);
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
    const [response] = await once(r, 'response');
    const t = response.pipe(tarx({
        cwd: extractPath,
    }));
    await once(t, 'end');
    await fs.promises.rename(path.join(extractPath, 'build'), buildPath);
}

export interface AVFrame {
    readonly width: number;
    readonly height: number;
    readonly pixelFormat: string;

    [Symbol.dispose](): void;
    destroy(): void;
    toBuffer(): Buffer;
    /**
     *
     * @param quality 1-31. Lower is better quality.
     */
    toJpeg(quality: number): Buffer;
    createEncoder(options: {
        encoder: string,
        bitrate: number,
        timeBaseNum: number,
        timeBaseDen: number,
        opts?: {
            [key: string]: string | number,
        },
    }): AVCodecContext;

    createFilter(options: {
        filter: string,
        timeBaseNum: number,
        timeBaseDen: number,
    }): AVFilter;
}

export interface AVFilter {
    [Symbol.dispose](): void;
    destroy(): void;
    setCrop(x: string, y: string, width: string, height: string): void;
    filter(frame: AVFrame): AVFrame;
}

export interface AVPacket {
    readonly streamIndex: number;
    readonly isKeyFrame: boolean;
    readonly size: number;
    readonly pts: number;
    readonly dts: number;
    readonly duration: number;

    [Symbol.dispose](): void;
    getData(): Buffer;
    destroy(): void;
}

export interface AVCodecContext {
    readonly hardwareDevice: string;
    /**
     * The pixel format of the output frames.
     * May not be available until the first frame is read.
     */
    readonly pixelFormat: string;
    readonly hardwarePixelFormat: string;
    readonly timeBaseNum: number;
    readonly timeBaseDen: number;

    [Symbol.dispose](): void;
    destroy(): void;
    sendPacket(packet: AVPacket): Promise<boolean>;
    receiveFrame(): Promise<AVFrame>;
    sendFrame(packet: AVFrame): Promise<boolean>;
    receivePacket(): Promise<AVPacket>;
}

export interface AVStream {
    readonly index: number;
    readonly timeBaseNum: number;
    readonly timeBaseDen: number;
    readonly codec: string;
    readonly type: string;
}

export interface AVFormatContext {
    readonly metadata: any;
    readonly streams: AVStream[];

    [Symbol.dispose](): void;
    open(input: string): void;
    createDecoder(streamIndex: number, hardwareDevice?: string, decoder?: string): AVCodecContext;
    readFrame(): Promise<AVPacket>;
    receiveFrame(streamIndex: number, codecContext: AVCodecContext): Promise<AVFrame>;
    create(format: string, callback: (buffer: Buffer) => void): void;
    newStream(options: {
        codecContext: AVCodecContext,
    }): number;
    writeFrame(streamIndex: number, packet: AVPacket): void;
    createSDP(): string;
    close(): void;
}

export function setAVLogCallback(callback: (msg: string, level: number) => void) {
    addon.setLogCallback(callback);
}

export function setAVLogLevel(level: 'quiet' | 'panic' | 'fatal' | 'error' | 'warning' | 'info' | 'verbose' | 'debug' | 'trace') {
    addon.setLogLevel(level);
}

export function createAVFormatContext(): AVFormatContext {
    return new addon.AVFormatContext();
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

export const version: string = packageJson.version;