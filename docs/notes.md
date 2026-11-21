 ldd --version | head -1; g++ --version | head -1; cmake --version | head -1

 ldd --version | head -1

objdump -T ~/.vst3/octet.vst3/Contents/x86_64-linux/octet.so \
  | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail

  danny@monokith:~/github/downspout$  ldd --version | head -1; g++ --version | head -1; cmake --version | head -1
ldd (Ubuntu GLIBC 2.43-2ubuntu2.7) 2.43
g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
cmake version 4.2.3
danny@monokith:~/github/downspout$  ldd --version | head -1
ldd (Ubuntu GLIBC 2.43-2ubuntu2.7) 2.43
danny@monokith:~/github/downspout$ objdump -T ~/.vst3/octet.vst3/Contents/x86_64-linux/octet.so \
  | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail
GLIBC_2.3
GLIBC_2.4
GLIBC_2.14
GLIBC_2.17
GLIBC_2.27
GLIBC_2.29
GLIBC_2.33
GLIBC_2.34
GLIBC_2.38
GLIBC_2.43