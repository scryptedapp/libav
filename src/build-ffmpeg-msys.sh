cd $(dirname $0)

cd ../nv-codec-headers
make install PREFIX=../_nvinstall

cd ../libvpl
export VPL_INSTALL_DIR=$PWD/../_vplinstall
cmake -B _build -DCMAKE_INSTALL_PREFIX=$VPL_INSTALL_DIR -DBUILD_SHARED_LIBS=OFF -DUSE_MSVC_STATIC_RUNTIME=ON -DMINGW_LIBS="-ladvapi32 -lmsvcrt -lole32"
cmake --build _build --config Release
cmake --install _build --config Release

cd ../FFmpeg

if [ ! -z "$FFMPEG_NO_REBUILD" ]
then
  if [ -f ffmpeg.exe ]
  then
    echo "FFmpeg already built, skipping build."
    exit 0
  fi
fi

# yasm can be dropped
pacman --noconfirm -S yasm nasm pkg-config clang diffutils make
export PKG_CONFIG_PATH=$PWD/../_vplinstall/lib/pkgconfig:$PWD/../_nvinstall/lib/pkgconfig:$PKG_CONFIG_PATH

# the --enable-nvdec is necessary for build sanity checking, the build will configure
# successfully if nvidia stuff is missing, even though it is required for --enable-cuda-llvm
# no open cl yet, need to do this https://github.com/m-ab-s/media-autobuild_suite/issues/1301#issuecomment-627800902
./configure --enable-libvpl --enable-cuda-llvm --enable-nvdec --toolchain=msvc && make -j32
exit $?
