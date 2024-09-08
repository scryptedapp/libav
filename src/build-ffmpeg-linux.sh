DEBIAN_FRONTEND=noninteractive
cd $(dirname $0)/../FFmpeg
sudo apt -y update
sudo apt -y install libva-dev libdrm-dev yasm

ARCH=$(arch)
if [ "$ARCH" = "x86_64" ]
then
    pushd ../nv-codec-headers
    make install PREFIX=../_nvinstall
    export PKG_CONFIG_PATH=$PWD/../_nvinstall/lib/pkgconfig:$PKG_CONFIG_PATH
    popd
    echo "Building with NVIDIA GPU and Vulkan support"
    export PATH=/usr/local/cuda-12.4/bin:$PATH
    ./configure --enable-vaapi --enable-libglslang --enable-cuda-llvm --enable-nvdec --extra-cflags=-I/usr/local/cuda/i>
else
    ./configure --enable-vaapi
fi

if [ "$?" != "0" ]
then
  exit 1
fi

make -j32
exit $?
