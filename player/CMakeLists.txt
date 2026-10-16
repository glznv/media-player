find_package(Threads REQUIRED)

add_executable(player
    src/main.cpp
    src/player.cpp
    src/clock.cpp
    src/player.hpp
    src/clock.hpp
    src/control.hpp
    src/bounded_queue.hpp
)

target_compile_features(player PRIVATE cxx_std_17)

set_target_properties(player PROPERTIES RUNTIME_OUTPUT_DIRECTORY
	"${RESULT_BINARY_DIR}")

target_link_libraries(player PRIVATE render audio media Threads::Threads)
