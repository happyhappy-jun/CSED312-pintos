FILE=bochs-2.6.11.tar.gz
if [ ! -f "$FILE" ]; then
  wget http://sourceforge.net/projects/bochs/files/bochs/2.6.11/bochs-2.6.11.tar.gz
fi
tar -xzvf bochs-2.6.11.tar.gz
cd bochs-2.6.11

cat ../bochs-2.6.11-jitter-plus-segv.patch | patch -p1
cat ../bochs-2.6.11-banner-stderr.patch | patch -p1
cat ../bochs-2.6.11-link-tinfo.patch | patch -p1

./configure --prefix=/usr/local \
  --disable-docbook \
  --enable-a20-pin \
  --enable-alignment-check \
  --enable-all-optimizations \
  --enable-avx \
  --enable-evex \
  --enable-cdrom \
  --enable-clgd54xx \
  --enable-cpu-level=6 \
  --enable-disasm \
  --enable-fpu \
  --enable-iodebug \
  --enable-large-ramfile \
  --enable-logging \
  --enable-long-phy-address \
  --enable-pci \
  --enable-plugins \
  --enable-readline \
  --enable-show-ips \
  --enable-usb \
  --enable-vmx=2 \
  --enable-x86-64 \
  --with-nogui \
  --with-sdl2 \
  --with-term \
  --with-x11 \
  --enable-gdb-stub

make
make install
cd ..
