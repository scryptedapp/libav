cd $(dirname $0)/../nv-codec-headers
make install PREFIX=../_nvinstall

cd $(dirname $0)/../FFmpeg
pacman --noconfirm -S yasm pkg-config clang diffutils make
export PKG_CONFIG_PATH=$PWD/../_vplinstall/lib/pkgconfig:$PWD/../_nvinstall/lib/pkgconfig:$PKG_CONFIG_PATH

# the --enable-nvdec is necessary for build sanity checking, the build will configure
# successfully if nvidia stuff is missing, even though it is required for --enable-cuda-llvm
./configure --enable-libvpl --enable-cuda-llvm --enable-nvdec --toolchain=msvc
make -j32
exit $?
