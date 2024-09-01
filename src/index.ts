import path from 'path';
import { fork } from 'child_process';

let addon: any;
try {
    addon = require('../build/Release/addon');
}
catch (e) {
}

export function isInstalled() {
    return !!addon;
}

export function loadAddon(addonPath: string) {
    addon = require(addonPath);
}

let installing: Promise<void> | undefined;
export async function install() {
    if (addon)
        return;

    if (installing)
        return installing;

    installing = new Promise<void>((resolve, reject) => {
        const cp = fork(require.resolve('prebuild-install/bin.js'), {
            cwd: path.dirname(__dirname),
        });

        cp.on('error', reject);

        cp.on('exit', (code) => {
            if (code === 0) {
                try {
                    addon = require('../build/Release/addon');
                    resolve();
                }
                catch (e) {
                    reject(e);
                }
            }
            else {
                reject(new Error('Failed to install'));
            }
        });
    })
    .finally(() => {
        installing = undefined;
    });

    return installing;
}

export interface AVFrame {
    close(): void;
    width: number;
    height: number;
    format: string;
    toBuffer(): Buffer;
    /**
     *
     * @param quality 1-31. Lower is better quality.
     */
    toJpeg(quality: number): Buffer;
}

export interface AVFilter {
    destroy(): void;
    setCrop(x: string, y: string, width: string, height: string): void;
    filter(frame: AVFrame): AVFrame;
}

export interface AVFormatContext {
    open(input: string, codec: string): void;
    readFrame(): Promise<AVFrame>;
    createFilter(width: number, height: number, filter: string, useHardware: boolean): AVFilter;
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
