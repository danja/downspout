2026-11-21 12:12:35.000000000 +0100 4335152 /home/danny/.vst3/octet.vst3/Contents/x86_64-linux/octet.so
8ff0d23515b985fcbb89416f190cc707a019168673a1ed0bcb2358400bf6b972  /home/danny/.vst3/octet.vst3/Contents/x86_64-linux/octet.so
--- GLIBC 2.43 symbols ---
0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.43) sqrtf
0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.43) atan2f
0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.43) log10f
0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.43) acosf
--- GLIBCXX versions ---
GLIBCXX_3.4
GLIBCXX_3.4.15
GLIBCXX_3.4.29
GLIBCXX_3.4.30
--- dynamic check ---
        linux-vdso.so.1 (0x000077f129297000)
        libdbus-1.so.3 => /usr/lib/x86_64-linux-gnu/libdbus-1.so.3 (0x000077f12904c000)
        libX11.so.6 => /usr/lib/x86_64-linux-gnu/libX11.so.6 (0x000077f128f0a000)
        libXcursor.so.1 => /usr/lib/x86_64-linux-gnu/libXcursor.so.1 (0x000077f128efd000)
        libXext.so.6 => /usr/lib/x86_64-linux-gnu/libXext.so.6 (0x000077f128ee9000)
        libXrandr.so.2 => /usr/lib/x86_64-linux-gnu/libXrandr.so.2 (0x000077f128edc000)
        libGL.so.1 => /usr/lib/x86_64-linux-gnu/libGL.so.1 (0x000077f128e55000)
        libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x000077f128a00000)
        libm.so.6 => /usr/lib/x86_64-linux-gnu/libm.so.6 (0x000077f128d2f000)
        libgcc_s.so.1 => /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 (0x000077f128d01000)
        libc.so.6 => /usr/lib/x86_64-linux-gnu/libc.so.6 (0x000077f128600000)
        libsystemd.so.0 => /usr/lib/x86_64-linux-gnu/libsystemd.so.0 (0x000077f1288ec000)
        libxcb.so.1 => /usr/lib/x86_64-linux-gnu/libxcb.so.1 (0x000077f128cd4000)
        libXrender.so.1 => /usr/lib/x86_64-linux-gnu/libXrender.so.1 (0x000077f128cc8000)
        libXfixes.so.3 => /usr/lib/x86_64-linux-gnu/libXfixes.so.3 (0x000077f128cc0000)
        libGLdispatch.so.0 => /usr/lib/x86_64-linux-gnu/libGLdispatch.so.0 (0x000077f128833000)
        libGLX.so.0 => /usr/lib/x86_64-linux-gnu/libGLX.so.0 (0x000077f1285cd000)
        /lib64/ld-linux-x86-64.so.2 (0x000077f129299000)
        libXau.so.6 => /usr/lib/x86_64-linux-gnu/libXau.so.6 (0x000077f128cb8000)
        libXdmcp.so.6 => /usr/lib/x86_64-linux-gnu/libXdmcp.so.6 (0x000077f128cb0000)
--- binary RPATH/RUNPATH ---
danny@monokith:~/github/downspout$ grep -E '^CXX_FLAGS' "$HOME/github/downspout/build/plugins/octet/CMakeFiles/octet-vst3.dir/flags.make" 2>/dev/null || true
CXX_FLAGS = -g -std=gnu++20 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fno-gnu-unique -Wall -Wextra
danny@monokith:~/github/downspout$ objdump -d -M intel "$HOME/.vst3/octet.vst3/Contents/x86_64-linux/octet.so" > /tmp/octet-monokith.dis

grep -Ein '\bymm[0-9]+\b|\bvzeroupper\b|\b(vaddps|vaddpd|vmulps|vmulpd|vsqrtps|vsqrtpd|vbroadcastss|vbroadcastsd|vpbroadcast|vfmadd|vfmsub)\b|\bpopcnt\b|\bcrc32\b|\b(blendps|blendpd|pblendw|pblendvb|pmulld|pcmpeqq|pcmpgtq|roundps|roundpd|roundss|roundsd)\b' /tmp/octet-monokith.dis | head -40
danny@monokith:~/github/downspout$ ldd "$(command -v reaper)" | grep -E 'libstdc\+\+|libc\.so'
        libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007a2b0ba00000)
        libc.so.6 => /usr/lib/x86_64-linux-gnu/libc.so.6 (0x00007a2b0b600000)