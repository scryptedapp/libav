cd $(dirname $0)/../FFmpeg
apt -y update
apt -y install libva-dev and libdrm-dev yasm
./configure --enable-vaapi --enable-shared
make -j32
exit $?
