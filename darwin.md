Mimimum macOS configuration for RTSP, h264, h265, jpg, and videotoolbox.

```sh
./configure \
    --disable-everything \
    --enable-hwaccels \
    --enable-videotoolbox \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-demuxer=rtsp \
    --enable-muxer=mjpeg \
    --enable-encoder=mjpeg \
    --enable-parser=mjpeg \
    --enable-protocol=tcp \
    --enable-protocol=udp \
    --enable-filter=crop \
    --enable-filter=scale \
    --enable-filter=scale_vt \
    --enable-filter=format \
    --enable-filter=hwdownload \
    --enable-filter=hwupload \
    --enable-parser=h264 \
    --enable-parser=hevc
```