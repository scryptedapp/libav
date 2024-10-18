DEBIAN_FRONTEND=noninteractive
cd $(dirname $0)/../FFmpeg

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

sudo apt -y update
# yasm can be dropped
sudo apt -y install libva-dev libdrm-dev yasm nasm cmake ocl-icd-opencl-dev

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
    ./configure --enable-libvpl --enable-vaapi --enable-opencl --enable-libglslang --enable-cuda-llvm --enable-nvdec --extra-cflags=-I/usr/local/cuda/include --extra-ldflags=-L/usr/local/cuda/lib64
else
    check_ffmpeg

    ./configure --enable-vaapi --enable-opencl
fi

if [ "$?" != "0" ]
then
  exit 1
fi

make -j32
exit $?
