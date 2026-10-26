cmake_minimum_required(VERSION 3.16)
project(gta3_weaponloader CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- host tests (any compiler):  cmake -S . -B build-host && cmake --build build-host && ctest --test-dir build-host
# (PNG/OGG reference tests need Python + Pillow/numpy + ffmpeg: run tests/run_all.sh for those)
add_executable(test_core tests/test_core.cpp)
enable_testing()
add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/pack/weaponloader)

# ---- the plugin must be a 32-bit DLL (GTA III is x86)
#   MSVC:   cmake -S . -B build -A Win32 -DWL_BUILD_PLUGIN=ON && cmake --build build --config Release
#   MinGW:  cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ -DWL_BUILD_PLUGIN=ON && cmake --build build
option(WL_BUILD_PLUGIN "Build the Windows ASI plugin" OFF)
if (WL_BUILD_PLUGIN)
  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GTA III is a 32-bit process: configure for Win32 / i686.")
  endif()
  add_library(weaponloader SHARED plugin/plugin.cpp)
  set_target_properties(weaponloader PROPERTIES PREFIX "" SUFFIX ".asi")
  target_link_libraries(weaponloader PRIVATE winmm)
  if (MSVC)
    target_compile_options(weaponloader PRIVATE /MT /EHsc)
  else()
    target_link_options(weaponloader PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at)
  endif()
endif()
