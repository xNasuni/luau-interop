rm -rf build_windows
mkdir build_windows

cmake -B build_windows \
      -DCMAKE_TOOLCHAIN_FILE=toolchain-zig-windows.cmake \
      -DLUAU_BUILD_SHARED_LIBS=ON \
      -DLUAU_BUILD_CLI=OFF \
      -DLUAU_BUILD_TESTS=OFF \
      .

make -C build_windows -j$(nproc)