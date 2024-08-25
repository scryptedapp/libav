import fs from 'fs';

async function main() {
    const addon = require('../../build/Release/addon');

    addon.set_console_callback((msg) => {
        console.log(msg);
    });
    
    const ctx = new addon.AVFormatContext();
    ctx.open("rtsp://192.168.2.124:54559/c19e89c2a12235b7", "videotoolbox");
    let filter: any;
    let jpgFilter: any;
    while (true ) {
        // break;
        const p =  ctx.readFrame();
        const frame = await p;
        if (!filter) {
            filter = ctx.createFilter(frame.width, frame.height, "crop=640:480:200:200,scale_vt=320:320,hwdownload,format=nv12,format=rgb24", true);
            jpgFilter = ctx.createFilter(frame.width, frame.height, `scale_vt=${frame.width}:${frame.height},hwdownload,format=nv12,format=yuvj420p`, true);
        }
        // choose a random crop within the frame width and height
        const cropLeft = Math.floor(Math.random() * frame.width);
        const cropTop = Math.floor(Math.random() * frame.height);
        const cropWidth = Math.floor(Math.random() * (frame.width - cropLeft));
        const cropHeight = Math.floor(Math.random() * (frame.height - cropTop));
        filter.setCrop(cropLeft.toString(), cropTop.toString(), cropWidth.toString(), cropHeight.toString());
        
    
        // ctx.filterFrame(frame, "", false);
        const ret = filter.filter(frame);
        const b: Buffer = ret.toBuffer();

        const jpgImage = jpgFilter.filter(frame);
        const jpg :Buffer = jpgImage.toJpeg();
        fs.writeFileSync('test.rgb', b);
        fs.writeFileSync('test.jpg', jpg);
        ret.close();
        frame.close();
        break;
    }
    
    // ctx.close();
    
}

main();
