DEBIAN_FRONTEND=noninteractive
cd $(dirname $0)/../FFmpeg
sudo apt -y update
sudo apt -y install libva-dev libdrm-dev yasm

ARCH=$(arch)
if [ "$ARCH" = "x86_64" ]
then
    pushd /tmp
    rm -rf nv-codec-headers
    git clone --depth 1 https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
    cd nv-codec-headers
    sudo make install
    popd
    echo "Building with NVIDIA GPU support"
    export PATH=/usr/local/cuda-12.4/bin:$PATH
    ./configure --enable-vaapi --enable-nonfree --enable-cuda-nvcc --extra-cflags=-I/usr/local/cuda/include --extra-ldflags=-L/usr/local/cuda/lib64
else
    ./configure --enable-vaapi
fi

make -j32
exit $?
