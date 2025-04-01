cd $(dirname $0)

cd ../opus
export OPUS_INSTALL_DIR=$PWD/../_opusinstall
cmake -B _build -DCMAKE_INSTALL_PREFIX=$OPUS_INSTALL_DIR -DBUILD_SHARED_LIBS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build _build --config Release
cmake --install _build --config Release
export PKG_CONFIG_PATH=$PWD/../_opusinstall/lib/pkgconfig:$PKG_CONFIG_PATH

cd ../openh264
MACOSX_DEPLOYMENT_TARGET=12.0 make -j32
make install-static PREFIX=$PWD/../_openh264install
export PKG_CONFIG_PATH=$PWD/../_openh264install/lib/pkgconfig:$PKG_CONFIG_PATH

cd ../FFmpeg

if [ ! -z "$FFMPEG_NO_REBUILD" ]
then
  if [ -f ffmpeg ]
  then
    echo "FFmpeg already built, skipping build."
    exit 0
  fi
fi

MACOSX_DEPLOYMENT_TARGET=12.0 ./configure --enable-libopenh264 --enable-encoder=libopenh264 --enable-libopus --enable-encoder=libopus --enable-decoder=libopus --disable-xlib --disable-sdl2 --disable-libxcb --enable-opencl --enable-videotoolbox --enable-neon --enable-pthreads --cc=clang && MACOSX_DEPLOYMENT_TARGET=12.0 make -j32
exit $?
