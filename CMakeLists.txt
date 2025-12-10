cmake_minimum_required(VERSION 3.10)
project(indexer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(indexer main.cpp)

target_compile_options(indexer PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-finput-charset=UTF-8 -fexec-charset=UTF-8>
)
