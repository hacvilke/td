cmake_minimum_required(VERSION 3.15)
project(TDEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Engine source files
file(GLOB_RECURSE ENGINE_SOURCES
    "src/core/*.cpp"
    "src/platform/*.cpp"
    "src/renderer/*.cpp"
    "src/physics/*.cpp"
    "src/audio/*.cpp"
    "src/net/*.cpp"
    "src/assets/*.cpp"
    "src/ecs/*.cpp"
)

# TD Scripting language sources
file(GLOB TD_SOURCES "src/td/*.cpp")

# Engine static library
add_library(td-engine STATIC ${ENGINE_SOURCES} ${TD_SOURCES})
target_include_directories(td-engine PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Platform-specific settings
if(WIN32)
    target_link_libraries(td-engine PUBLIC
        opengl32
        gdi32
        user32
        winmm
        ws2_32
    )
    target_compile_definitions(td-engine PUBLIC WIN32 _WINDOWS)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(td-engine PRIVATE /W4)
else()
    target_compile_options(td-engine PRIVATE -Wall -Wextra)
endif()

# Examples
add_executable(pong examples/pong/main.cpp)
target_link_libraries(pong PRIVATE td-engine)

add_executable(platformer examples/platformer/main.cpp)
target_link_libraries(platformer PRIVATE td-engine)

# Tests
add_executable(test_math tests/test_math.cpp)
target_link_libraries(test_math PRIVATE td-engine)

add_executable(test_ecs tests/test_ecs.cpp)
target_link_libraries(test_ecs PRIVATE td-engine)

add_executable(test_physics tests/test_physics.cpp)
target_link_libraries(test_physics PRIVATE td-engine)

# Copy assets
add_custom_command(TARGET pong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:pong>/assets
)

# Install targets
install(TARGETS td-engine ARCHIVE DESTINATION lib)
install(DIRECTORY src/ DESTINATION include/td-engine
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY assets/ DESTINATION share/td-engine/assets)

# Editor (if sources exist)
if(EXISTS "${CMAKE_SOURCE_DIR}/editor/main.cpp")
    file(GLOB EDITOR_SOURCES "editor/*.cpp")
    add_executable(td-editor ${EDITOR_SOURCES})
    target_link_libraries(td-editor PRIVATE td-engine)
endif()
