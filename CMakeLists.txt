cmake_minimum_required(VERSION 3.14)
project(player LANGUAGES CXX)

set(RESULT_BINARY_DIR "${CMAKE_BINARY_DIR}/bin")

option(BUILD_TESTS "Test individual components of the player" OFF)

add_subdirectory(render)
add_subdirectory(audio)
add_subdirectory(media)
