cmake_minimum_required(VERSION 3.15)

project(pearDB VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True) # do not silently fail back to older version of cpp


# file(GLOB_RECURSE SOURCES "src/*.cpp") # glob recurse through all cpp files in source

# main executable 
file(GLOB_RECURSE LIB_SOURCES "src/storage/*.cpp" "src/index/*.cpp" "src/db.cpp")

add_executable(pearDB src/main.cpp ${LIB_SOURCES}) # compile into executable called pearDB
target_include_directories(pearDB PRIVATE src)
target_compile_options(pearDB PRIVATE -Wall -Wextra -Wpedantic) # enable compiler warnings

# test executable 
file(GLOB TEST_SOURCES "tests/*.cpp")
add_executable(pearDB_tests ${TEST_SOURCES} ${LIB_SOURCES})
target_include_directories(pearDB_tests PRIVATE src)
target_compile_options(pearDB_tests PRIVATE -Wall -Wextra)

# benchmark executable 
add_executable(pearDB_bench benchmarks/benchmarks.cpp ${LIB_SOURCES})
target_include_directories(pearDB_bench PRIVATE src)

target_compile_options(pearDB_bench PRIVATE -O2)

# custom targets 
add_custom_target(
    run_tests
    COMMAND ./pearDB_tests
    DEPENDS pearDB_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(
    run_bench
    COMMAND ./pearDB_bench
    DEPENDS pearDB_bench
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
