cd $(dirname $0)

DEBIAN_FRONTEND=noninteractive

cd ../opus
export OPUS_INSTALL_DIR=$PWD/../_opusinstall
cmake -B _build -DCMAKE_INSTALL_PREFIX=$OPUS_INSTALL_DIR -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build _build --config Release
cmake --install _build --config Release
# hack to make ffmpeg configure opus properly due to missing libm
sed -i '/^Libs:/ s/$/ -lm/' ../_opusinstall/lib/pkgconfig/opus.pc
export PKG_CONFIG_PATH=$PWD/../_opusinstall/lib/pkgconfig:$PKG_CONFIG_PATH

cd ../openh264
make -j32
make install-static PREFIX=$PWD/../_openh264install
export PKG_CONFIG_PATH=$PWD/../_openh264install/lib/pkgconfig:$PKG_CONFIG_PATH

function check_ffmpeg() {
  if [ ! -z "$FFMPEG_NO_REBUILD" ]
  then
    if [ -f ffmpeg ]
    then
      echo "FFmpeg already built, skipping build."
      exit 0
    fi
  fi
}

cd ../FFmpeg

ARCH=$(arch)
if [ "$ARCH" = "x86_64" ]
then
    pushd ../libvpl
    export VPL_INSTALL_DIR=`pwd`/../_vplinstall
    cmake -B _build -DCMAKE_INSTALL_PREFIX=$VPL_INSTALL_DIR -DBUILD_SHARED_LIBS=OFF
    cmake --build _build
    cmake --install _build
    export PKG_CONFIG_PATH=$PWD/../_vplinstall/lib/pkgconfig:$PKG_CONFIG_PATH
    popd

    pushd ../nv-codec-headers
    make install PREFIX=../_nvinstall
    export PKG_CONFIG_PATH=$PWD/../_nvinstall/lib/pkgconfig:$PKG_CONFIG_PATH
    popd

    check_ffmpeg

    echo "Building with NVIDIA GPU and Vulkan support"
    export PATH=/usr/local/cuda-12.4/bin:$PATH
    ./configure --enable-libopenh264 --enable-gnutls --enable-encoder=libopenh264 --enable-libopus --enable-encoder=libopus --enable-decoder=libopus --enable-libvpl --enable-vaapi --enable-opencl --enable-libglslang --enable-cuda-llvm --enable-nvdec --extra-cflags=-I/usr/local/cuda/include --extra-ldflags=-L/usr/local/cuda/lib64
else
    check_ffmpeg

    ./configure --enable-libopenh264 --enable-gnutls --enable-encoder=libopenh264 --enable-libopus --enable-encoder=libopus --enable-decoder=libopus --enable-vaapi --enable-opencl
fi

if [ "$?" != "0" ]
then
  exit 1
fi

make -j32
exit $?
