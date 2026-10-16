find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(render STATIC
    src/x11/display.cpp
    src/x11/window.cpp
    src/glx/gl_context.cpp
    src/gl/gl_renderer.cpp
    include/render/display.hpp
    include/render/window.hpp
    include/render/event.hpp
    include/render/frame.hpp
    include/render/gl_loader.hpp
    include/render/gl_context.hpp
    include/render/gl_renderer.hpp
    include/render/platform/x11.hpp
)

target_compile_features(render PUBLIC cxx_std_17)

target_include_directories(render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(render PRIVATE ${X11_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR})

target_link_libraries(render PRIVATE ${X11_LIBRARIES} ${OPENGL_LIBRARIES})

if(BUILD_TESTS)
	add_executable(test_render test.cpp)

	set_target_properties(test_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY
		"${RESULT_BINARY_DIR}")

    target_link_libraries(test_render PRIVATE render)
endif()
