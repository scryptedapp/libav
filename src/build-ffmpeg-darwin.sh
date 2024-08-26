cd $(dirname $0)/../FFmpeg
MACOSX_DEPLOYMENT_TARGET=12.0 ./configure --enable-videotoolbox --enable-neon --enable-pthreads --cc=clang && MACOSX_DEPLOYMENT_TARGET=12.0 make -j32
exit $?
