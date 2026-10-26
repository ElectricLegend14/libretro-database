cmake_minimum_required(VERSION 3.20)
project(rf4s_coop C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                      $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
endif()

# Portable core: protocol, networking, sync. Builds and tests on any OS.
add_library(coop_core STATIC
  src/protocol.cpp
  src/connection.cpp
  src/transport.cpp
  src/udp.cpp
  src/session.cpp
  src/signature.cpp
  src/config.cpp
  src/layout.cpp
  src/pe.cpp
  src/sim_game.cpp
  src/storage.cpp)
target_include_directories(coop_core PUBLIC include)
if(WIN32)
  target_link_libraries(coop_core PUBLIC ws2_32)
endif()

add_executable(coop_sim tools/coop_sim.cpp)
target_link_libraries(coop_sim PRIVATE coop_core)

add_executable(sigscan tools/sigscan.cpp)
target_link_libraries(sigscan PRIVATE coop_core)

add_executable(layoutcheck tools/layoutcheck.cpp)
target_link_libraries(layoutcheck PRIVATE coop_core)

enable_testing()
add_executable(coop_tests tests/tests.cpp)
target_link_libraries(coop_tests PRIVATE coop_core)
add_test(NAME coop_tests COMMAND coop_tests)

# The mod itself and its injector: 64-bit Windows only.
if(WIN32)
  # MinHook (BSD-2-Clause) puts the frame hook in place. Fetched at a pinned
  # commit; set RF4S_COOP_MINHOOK_DIR to a local copy to build offline.
  set(RF4S_COOP_MINHOOK_DIR "" CACHE PATH "Local MinHook source tree (optional)")
  if(RF4S_COOP_MINHOOK_DIR)
    set(minhook_SOURCE_DIR "${RF4S_COOP_MINHOOK_DIR}")
  else()
    include(FetchContent)
    FetchContent_Declare(minhook
      GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
      GIT_TAG 9fbd087432700d73fc571118d6a9697a36443d88)  # v1.3.3
    FetchContent_MakeAvailable(minhook)  # no CMakeLists of its own: this only downloads
  endif()
  add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
  target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

  add_library(rf4s-coop SHARED src/win/dllmain.cpp src/win/rf4s_adapter.cpp src/win/frame_hook.cpp)
  if(MSVC)
    enable_language(ASM_MASM)
    target_sources(rf4s-coop PRIVATE src/win/frame_thunk.asm)
  endif()
  target_link_libraries(rf4s-coop PRIVATE coop_core minhook)
  set_target_properties(rf4s-coop PROPERTIES PREFIX "")

  add_executable(rf4s-coop-inject tools/inject.cpp)

  if(MINGW)
    # Ship without needing MinGW's runtime DLLs next to the game.
    target_link_options(rf4s-coop PRIVATE -static)
    target_link_options(rf4s-coop-inject PRIVATE -static)
    target_link_options(coop_sim PRIVATE -static)
    target_link_options(sigscan PRIVATE -static)
    target_link_options(layoutcheck PRIVATE -static)
    target_link_options(coop_tests PRIVATE -static)
  endif()
endif()
