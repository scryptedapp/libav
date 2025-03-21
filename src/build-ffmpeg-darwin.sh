cd $(dirname $0)/../FFmpeg

if [ ! -z "$FFMPEG_NO_REBUILD" ]
then
  if [ -f ffmpeg ]
  then
    echo "FFmpeg already built, skipping build."
    exit 0
  fi
fi

MACOSX_DEPLOYMENT_TARGET=12.0 ./configure --enable-libopus --disable-xlib --disable-sdl2 --disable-libxcb --enable-opencl --enable-videotoolbox --enable-neon --enable-pthreads --cc=clang && MACOSX_DEPLOYMENT_TARGET=12.0 make -j32
exit $?
