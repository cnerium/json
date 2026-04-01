cmake_minimum_required(VERSION 3.20)

project(cnerium_json
    VERSION 0.1.0
    DESCRIPTION "Cnerium JSON - Lightweight JSON utilities"
    LANGUAGES CXX
)

include(GNUInstallDirs)

# ========================
# Options
# ========================
option(CNERIUM_JSON_BUILD_TESTS "Build json tests" ON)
option(CNERIUM_JSON_BUILD_EXAMPLES "Build json examples" ON)

# ========================
# Global settings
# ========================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ========================
# Library (header-only)
# ========================
add_library(cnerium_json INTERFACE)
add_library(cnerium::json ALIAS cnerium_json)

target_compile_features(cnerium_json INTERFACE cxx_std_20)

target_include_directories(cnerium_json
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# ========================
# Compiler options
# ========================
if(MSVC)
    target_compile_options(cnerium_json INTERFACE /W4 /permissive-)
else()
    target_compile_options(cnerium_json INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ========================
# Target properties
# ========================
set_target_properties(cnerium_json PROPERTIES
    EXPORT_NAME json
)

# ========================
# Examples
# ========================
if(CNERIUM_JSON_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ========================
# Tests
# ========================
if(CNERIUM_JSON_BUILD_TESTS)
    include(CTest)
    enable_testing()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_basic.cpp")
        add_executable(cnerium_json_test_basic tests/test_basic.cpp)
        target_link_libraries(cnerium_json_test_basic PRIVATE cnerium::json)
        target_compile_features(cnerium_json_test_basic PRIVATE cxx_std_20)

        if(MSVC)
            target_compile_options(cnerium_json_test_basic PRIVATE /W4 /permissive-)
        else()
            target_compile_options(cnerium_json_test_basic PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        add_test(NAME cnerium_json.basic COMMAND cnerium_json_test_basic)
    endif()
endif()

# ========================
# Summary
# ========================
message(STATUS "------------------------------------------------------")
message(STATUS "cnerium::json configured (${PROJECT_VERSION})")
message(STATUS "Include dir:     ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "Build tests:     ${CNERIUM_JSON_BUILD_TESTS}")
message(STATUS "Build examples:  ${CNERIUM_JSON_BUILD_EXAMPLES}")
message(STATUS "Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "------------------------------------------------------")
