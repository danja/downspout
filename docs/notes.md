 ldd --version | head -1; g++ --version | head -1; cmake --version | head -1

 ldd --version | head -1

objdump -T ~/.vst3/octet.vst3/Contents/x86_64-linux/octet.so \
  | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail