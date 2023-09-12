FILE=bochs-2.6.11.tar.gz
if [ ! -f "$FILE" ]; then
  wget http://sourceforge.net/projects/bochs/files/bochs/2.6.8/bochs-2.6.11.tar.gz
fi
tar -xzvf bochs-2.6.11.tar.gz
cd bochs-2.6.11

cat ../bochs-2.6.11-jitter-plus-segv.patch | patch -p1
cat ../bochs-2.6.11-banner-stderr.patch | patch -p1
cat ../bochs-2.6.11-link-tinfo.patch | patch -p1

cd ..
tar czvf bochs-2.6.11.tar.gz bochs-2.6.11
cp bochs-2.6.11.tar.gz /tmp/bochs-2.6.11.tar.gz

brew install --build-from-source bochs.rb