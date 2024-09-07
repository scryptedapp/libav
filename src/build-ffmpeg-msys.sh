cd $(dirname $0)/../FFmpeg
pacman --noconfirm -S yasm pkg-config clang diffutils make
export PKG_CONFIG_PATH=/c/Users/koush/OneDrive/Desktop/work/_vplinstall/lib/pkgconfig:/c/Users/koush/OneDrive/Desktop/work/_nvinstall/lib/pkgconfig:$PKG_CONFIG_PATH

# the --enable-nvdec is necessary for build sanity checking, the build will configure
# successfully if nvidia stuff is missing, even though it is required for --enable-cuda-llvm
./configure --enable-libvpl --enable-cuda-llvm --enable-nvdec --toolchain=msvc && make -j32
exit $?
