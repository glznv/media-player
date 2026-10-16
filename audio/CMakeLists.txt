find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

add_library(audio STATIC
    src/alsa/output.cpp
    include/audio/output.hpp
)

target_compile_features(audio PUBLIC cxx_std_17)

target_include_directories(audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(audio PRIVATE ${ALSA_INCLUDE_DIRS})

target_link_libraries(audio PRIVATE ${ALSA_LIBRARIES} Threads::Threads)

if(BUILD_TESTS)
	add_executable(test_audio test.cpp)

	set_target_properties(test_audio PROPERTIES RUNTIME_OUTPUT_DIRECTORY
		"${RESULT_BINARY_DIR}")

    target_link_libraries(test_audio PRIVATE audio Threads::Threads)
endif()
