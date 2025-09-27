emcmake cmake -B build_web -DLUAU_BUILD_WEB=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS="-sSTACK_SIZE=1048576"
cmake --build build_web -j2 --target Luau.Web