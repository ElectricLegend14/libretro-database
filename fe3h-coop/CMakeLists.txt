cmake_minimum_required(VERSION 3.16)
project(fe3h_coop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-independent core: settings, roster, ownership rules.
# The Switch build links the same sources together with src/switch/hooks.cpp.
add_library(coop_core
    src/color.cpp
    src/roster.cpp
    src/settings.cpp
    src/coop_state.cpp
)
target_include_directories(coop_core PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(coop_core PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_executable(coop_tests tests/test_main.cpp)
target_link_libraries(coop_tests PRIVATE coop_core)
target_compile_definitions(coop_tests PRIVATE COOP_SAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/coop.ini")
add_test(NAME coop_tests COMMAND coop_tests)
