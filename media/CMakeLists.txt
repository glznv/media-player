find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat>=59.27.100
    libavcodec>=59.37.100
    libavutil>=57.28.100
    libswscale>=6.7.100
    libswresample>=4.7.100
)

add_library(media STATIC
    src/ffmpeg/internal.hpp
    src/ffmpeg/frames.cpp
    src/ffmpeg/demuxer.cpp
    src/ffmpeg/decoder.cpp
    src/ffmpeg/video_decoder.cpp
    src/ffmpeg/audio_decoder.cpp
    include/media/types.hpp
    include/media/packet.hpp
    include/media/video_frame.hpp
    include/media/demuxer.hpp
    include/media/video_decoder.hpp
    include/media/audio_decoder.hpp
)

target_compile_features(media PUBLIC cxx_std_17)

target_include_directories(media PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(media PRIVATE PkgConfig::FFMPEG)

if(BUILD_TESTS)
	add_executable(test_media test.cpp)

	set_target_properties(test_media PROPERTIES RUNTIME_OUTPUT_DIRECTORY
		"${RESULT_BINARY_DIR}")

    target_link_libraries(test_media PRIVATE media)
endif()
