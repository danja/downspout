 ldd --version | head -1; g++ --version | head -1; cmake --version | head -1

 ldd --version | head -1

objdump -T ~/.vst3/octet.vst3/Contents/x86_64-linux/octet.so \
  | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail

 
SO="$HOME/.vst3/octet.vst3/Contents/x86_64-linux/octet.so"

stat -c '%y %s %n' "$SO"
sha256sum "$SO"

echo "--- GLIBC 2.43 symbols ---"
objdump -T "$SO" | grep 'GLIBC_2.43'

echo "--- GLIBCXX versions ---"
objdump -T "$SO" | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail

echo "--- dynamic check ---"
ldd -r "$SO"

echo "--- binary RPATH/RUNPATH ---"
readelf -d "$SO" | grep -E 'RPATH|RUNPATH' || true
And check the actual compile flags if that machine has a local build tree:
grep -E '^CXX_FLAGS' "$HOME/github/downspout/build/plugins/octet/CMakeFiles/octet-vst3.dir/flags.make" 2>/dev/null || true
To check the Athlon II instruction theory directly on that exact binary:
objdump -d -M intel "$HOME/.vst3/octet.vst3/Contents/x86_64-linux/octet.so" > /tmp/octet-monokith.dis

grep -Ein '\bymm[0-9]+\b|\bvzeroupper\b|\b(vaddps|vaddpd|vmulps|vmulpd|vsqrtps|vsqrtpd|vbroadcastss|vbroadcastsd|vpbroadcast|vfmadd|vfmsub)\b|\bpopcnt\b|\bcrc32\b|\b(blendps|blendpd|pblendw|pblendvb|pmulld|pcmpeqq|pcmpgtq|roundps|roundpd|roundss|roundsd)\b' /tmp/octet-monokith.dis | head -40
Also worth checking:
ldd "$(command -v reaper)" | grep -E 'libstdc\+\+|libc\.so'