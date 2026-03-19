cmake_minimum_required(VERSION 3.20)
project(cnerium_json LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========================
# Library (header-only)
# ========================
add_library(json INTERFACE)
add_library(json::json ALIAS json)

target_include_directories(json INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if (MSVC)
    target_compile_options(json INTERFACE /W4 /permissive-)
else()
    target_compile_options(json INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ========================
# Options
# ========================
option(JSON_BUILD_EXAMPLES "Build examples" ON)
option(JSON_BUILD_TESTS "Build tests" ON)

# ========================
# Examples
# ========================
if (JSON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ========================
# Tests
# ========================
if (JSON_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(json_basic_test tests/test_basic.cpp)
    target_link_libraries(json_basic_test PRIVATE json::json)

    add_test(NAME json.basic COMMAND json_basic_test)
endif()
