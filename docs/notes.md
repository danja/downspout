grep '^CMAKE_BUILD_TYPE' "$HOME/github/downspout/build/CMakeCache.txt"

cd "$HOME/github/downspout"
DOWNSPOUT_BUILD_DIR="$PWD/build-release" DOWNSPOUT_RUN_TESTS=0 ./install.sh
Then verify:
grep '^CXX_FLAGS' "$PWD/build-release/plugins/octet/CMakeFiles/octet-vst3.dir/flags.make"
sha256sum "$HOME/.vst3/octet.vst3/Contents/x86_64-linux/octet.so"
You want to see -O3 -DNDEBUG again. After installing a Release build, force REAPER to rescan or use a fresh portable REAPER config before loading Octet.