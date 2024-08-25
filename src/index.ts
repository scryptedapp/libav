let addon: any;
try {
    addon = require('../build/Release/addon');
}
catch (e) {
}

export function isAvailable() {
    return !!addon;
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

export function setAVLogCallback(callback: (msg: string) => void) {
    addon.set_console_callback(callback);
}

export function createAVFormatContext(): AVFormatContext {
    return new addon.AVFormatContext();
}
