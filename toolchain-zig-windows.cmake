set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(ZIG_EXECUTABLE zig REQUIRED)

set(ZIG_WRAPPER_DIR "${CMAKE_BINARY_DIR}/zig-wrappers")
file(MAKE_DIRECTORY "${ZIG_WRAPPER_DIR}")

set(ZIG_CC_WRAPPER "${ZIG_WRAPPER_DIR}/zig-cc")
set(ZIG_CXX_WRAPPER "${ZIG_WRAPPER_DIR}/zig-cxx")
set(ZIG_RC_WRAPPER "${ZIG_WRAPPER_DIR}/zig-rc")

file(WRITE "${ZIG_CC_WRAPPER}" "#!/bin/bash\nexec ${ZIG_EXECUTABLE} cc -target x86_64-windows-gnu \"$@\"\n")
file(WRITE "${ZIG_CXX_WRAPPER}" "#!/bin/bash\nexec ${ZIG_EXECUTABLE} c++ -target x86_64-windows-gnu -lc++ \"$@\"\n")
file(WRITE "${ZIG_RC_WRAPPER}" "#!/bin/bash\nexec ${ZIG_EXECUTABLE} rc \"$@\"\n")

execute_process(COMMAND chmod +x "${ZIG_CC_WRAPPER}")
execute_process(COMMAND chmod +x "${ZIG_CXX_WRAPPER}")
execute_process(COMMAND chmod +x "${ZIG_RC_WRAPPER}")

set(CMAKE_C_COMPILER "${ZIG_CC_WRAPPER}")
set(CMAKE_CXX_COMPILER "${ZIG_CXX_WRAPPER}")
set(CMAKE_RC_COMPILER "${ZIG_RC_WRAPPER}")

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
set(CMAKE_RC_COMPILER_WORKS 1)

set(CMAKE_C_ABI_COMPILED 1)
set(CMAKE_CXX_ABI_COMPILED 1)

set(CMAKE_CXX_COMPILE_FEATURES 
    cxx_std_98
    cxx_std_11
    cxx_std_14
    cxx_std_17
    cxx_std_20
    cxx_std_23
)

set(CMAKE_C_COMPILE_FEATURES
    c_std_90
    c_std_99
    c_std_11
    c_std_17
    c_std_23
)

set(CMAKE_EXECUTABLE_SUFFIX ".exe")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_SHARED_MODULE_SUFFIX ".dll")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared -Wl,--out-implib,<TARGET_IMPLIB>")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "-shared -Wl,--out-implib,<TARGET_IMPLIB>")

set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")

set(CMAKE_IMPORT_LIBRARY_SUFFIX ".dll.a")