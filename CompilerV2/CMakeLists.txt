cmake_minimum_required(VERSION 3.20)
project(lunaris_compiler_v2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUNARIS_V2_BUILD_TESTS "Build compiler tests" ON)

add_library(lunaris_compiler_v2_core
    src/diagnostic.cpp
    src/machine_backend.cpp
    src/codegen.cpp
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/token.cpp
)

target_include_directories(lunaris_compiler_v2_core PUBLIC src)

target_compile_features(lunaris_compiler_v2_core PUBLIC cxx_std_20)

add_executable(lunaris_compiler_v2
    src/main.cpp
)

target_link_libraries(lunaris_compiler_v2 PRIVATE lunaris_compiler_v2_core)

if (LUNARIS_V2_BUILD_TESTS)
    enable_testing()
    add_executable(lunaris_compiler_v2_tests
        tests/test_main.cpp
        tests/lexer_tests.cpp
        tests/parser_tests.cpp
    )
    target_link_libraries(lunaris_compiler_v2_tests PRIVATE lunaris_compiler_v2_core)
    add_test(NAME lunaris_compiler_v2_tests COMMAND lunaris_compiler_v2_tests)
endif()
