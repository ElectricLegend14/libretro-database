cmake_minimum_required(VERSION 3.20)
project(rf4s_coop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Portable core: protocol, networking, sync. Builds and tests on any OS.
add_library(coop_core STATIC
  src/protocol.cpp
  src/udp.cpp
  src/session.cpp
  src/signature.cpp
  src/config.cpp
  src/sim_game.cpp)
target_include_directories(coop_core PUBLIC include)
if(WIN32)
  target_link_libraries(coop_core PUBLIC ws2_32)
endif()

add_executable(coop_sim tools/coop_sim.cpp)
target_link_libraries(coop_sim PRIVATE coop_core)

add_executable(sigscan tools/sigscan.cpp)
target_link_libraries(sigscan PRIVATE coop_core)

enable_testing()
add_executable(coop_tests tests/tests.cpp)
target_link_libraries(coop_tests PRIVATE coop_core)
add_test(NAME coop_tests COMMAND coop_tests)

# The mod itself and its injector: 64-bit Windows only.
if(WIN32)
  add_library(rf4s-coop SHARED src/win/dllmain.cpp src/win/rf4s_adapter.cpp)
  target_link_libraries(rf4s-coop PRIVATE coop_core)
  set_target_properties(rf4s-coop PROPERTIES PREFIX "")

  add_executable(rf4s-coop-inject tools/inject.cpp)

  if(MINGW)
    # Ship without needing MinGW's runtime DLLs next to the game.
    target_link_options(rf4s-coop PRIVATE -static)
    target_link_options(rf4s-coop-inject PRIVATE -static)
    target_link_options(coop_sim PRIVATE -static)
    target_link_options(sigscan PRIVATE -static)
    target_link_options(coop_tests PRIVATE -static)
  endif()
endif()
