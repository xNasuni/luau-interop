rm -rf build_macos
mkdir build_macos

cmake -B build_macos \
      -DCMAKE_TOOLCHAIN_FILE=toolchain-zig-macos.cmake \
      -DLUAU_BUILD_SHARED_LIBS=ON \
      -DLUAU_BUILD_CLI=OFF \
      -DLUAU_BUILD_TESTS=OFF \
      .

make -C build_macos -j$(nproc)