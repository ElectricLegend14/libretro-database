cmake_minimum_required(VERSION 3.16)
project(fe3h_coop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-independent core: settings, roster, ownership and turn rules,
# input routing, hook lookup and NSO reading. switch/build.sh copies the
# same sources into the exlaunch build next to switch/main.cpp.
add_library(coop_core
    src/color.cpp
    src/coop_state.cpp
    src/hook_targets.cpp
    src/ini.cpp
    src/input_router.cpp
    src/nso.cpp
    src/roster.cpp
    src/settings.cpp
    src/sigscan.cpp
)
target_include_directories(coop_core PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(coop_core PRIVATE -Wall -Wextra)
endif()

add_executable(coop_check tools/coop_check.cpp)
target_link_libraries(coop_check PRIVATE coop_core)

enable_testing()
add_executable(coop_tests tests/test_main.cpp)
target_link_libraries(coop_tests PRIVATE coop_core)
target_compile_definitions(coop_tests PRIVATE
    COOP_SAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/coop.ini"
    COOP_SAMPLE_OFFSETS="${CMAKE_CURRENT_SOURCE_DIR}/config/offsets.ini"
    COOP_SAMPLE_NSO="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample.nso")
add_test(NAME coop_tests COMMAND coop_tests)
