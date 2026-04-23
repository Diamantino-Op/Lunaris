cmake_minimum_required(VERSION 3.20)
project(lunaris_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUNARIS_BUILD_TESTS "Build compiler tests" ON)

add_library(lunaris_compiler_core
    src/diagnostic.cpp
    src/codegen.cpp
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/token.cpp
)

target_include_directories(lunaris_compiler_core PUBLIC src)

target_compile_features(lunaris_compiler_core PUBLIC cxx_std_20)

add_executable(lunaris_compiler
    src/main.cpp
)

target_link_libraries(lunaris_compiler PRIVATE lunaris_compiler_core)

if (LUNARIS_BUILD_TESTS)
    enable_testing()
    add_executable(lunaris_compiler_tests
        tests/test_main.cpp
        tests/lexer_tests.cpp
        tests/parser_tests.cpp
    )
    target_link_libraries(lunaris_compiler_tests PRIVATE lunaris_compiler_core)
    add_test(NAME lunaris_compiler_tests COMMAND lunaris_compiler_tests)
endif()
