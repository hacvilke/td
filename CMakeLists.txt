# =============================================================================
# TD Engine - CMakeLists.txt
#
# Two build modes:
#
#   1. Desktop build (default)
#        cmake -B build-desktop && cmake --build build-desktop
#      Uses the system C++ compiler + Win32 platform layer.
#
#   2. WebAssembly build (requires Emscripten)
#        emcmake cmake -B build-web -DTD_BUILD_WEB=ON
#        cmake --build build-web
#      Output: web/td-engine.js + web/td-engine.wasm
#
# Common options:
#   -DTD_BUILD_EDITOR=ON   Build the desktop editor (default ON)
#   -DTD_BUILD_EXAMPLES=ON Build pong + platformer (default ON)
#   -DTD_BUILD_TESTS=ON    Build unit tests (default ON)
# =============================================================================

cmake_minimum_required(VERSION 3.15)
project(TDEngine VERSION 1.0.0 LANGUAGES CXX)

# --- Build options -----------------------------------------------------------
option(TD_BUILD_WEB      "Build for WebAssembly (requires Emscripten)" OFF)
option(TD_BUILD_EDITOR   "Build the desktop editor"                     ON)
option(TD_BUILD_EXAMPLES "Build pong + platformer examples"             ON)
option(TD_BUILD_TESTS    "Build unit tests"                              ON)

# --- C++ standard ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT TD_BUILD_WEB)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Output directories ------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Engine sources (shared between desktop and web)
# =============================================================================
# These are the engine's portable subsystem sources. Both builds compile them
# unchanged - the platform layer is the only thing that differs.
#
# Tier 1/2 modules added in the modularity pass:
#   - src/scripting/script_vm.cpp    Lua VM stub (Tier 1.3, real impl TODO)
# The other Tier 1/2/3 modules (profiler, signal, scene, serializer,
# transport, voxel, ui, character_controller, server_authoritative) are
# header-only — they live in .h files and are #included directly by callers.
# This keeps the build graph small while the modules are still skeletons;
# they'll graduate to .cpp files when the real implementations land.
set(ENGINE_PORTABLE_SOURCES
    src/core/logger.cpp
    src/core/game_loop.cpp
    src/renderer/gl_renderer.cpp
    src/renderer/sprite_batch.cpp
    src/renderer/camera.cpp
    src/renderer/framebuffer.cpp
    src/renderer/gl_shader.cpp
    src/renderer/texture.cpp
    src/renderer/mesh.cpp
    src/physics/aabb.cpp
    src/physics/collision.cpp
    src/physics/rigidbody.cpp
    src/physics/character_controller.cpp
    src/audio/wav_loader.cpp
    src/audio/mixer.cpp
    src/audio/spatial_audio.cpp
    src/assets/png_decoder.cpp
    src/assets/obj_loader.cpp
    src/assets/asset_loader.cpp
    src/ecs/world.cpp
    src/ecs/entity.cpp
    src/ecs/beat_system.cpp
    src/scripting/script_vm.cpp
    src/ui/ui.cpp
    src/voxel/voxel.cpp
    src/net/transport.cpp
    src/net/server_authoritative.cpp
)

# NOTE: src/td/ (custom scripting VM) and src/net/ (Winsock socket/server/client)
# were removed in the engine cleanup pass. The scripting VM was never wired up
# to a C API or JS bridge; networking was never used by any example/editor/test.
# Both will be re-added properly per docs/MODULARITY_ROADMAP.md:
#   - scripting: Lua VM (Tier 1) instead of a custom language
#   - networking: ENet or WebRTC (Tier 1) instead of raw Winsock

# =============================================================================
if(TD_BUILD_WEB)
# =============================================================================
# WebAssembly (Emscripten) build
# =============================================================================
    # Verify Emscripten toolchain is in use.
    if(NOT CMAKE_C_COMPILER MATCHES "emcc")
        message(WARNING
            "TD_BUILD_WEB=ON but C compiler is not emcc. Use:\n"
            "  emcmake cmake -B build-web -DTD_BUILD_WEB=ON\n"
            "Setting compiler to emcc/em++ explicitly.")
        set(CMAKE_C_COMPILER emcc)
        set(CMAKE_CXX_COMPILER em++)
    endif()

    # The platform layer is replaced by the Emscripten main. We do NOT compile
    # win32_window.cpp / win32_input.cpp / audio_engine.cpp (waveOut) /
    # socket.cpp (Winsock) - the browser provides those via the JS bridge.
    set(PLATFORM_SOURCES wasm/emscripten_main.cpp)

    add_executable(td-engine ${ENGINE_PORTABLE_SOURCES} ${PLATFORM_SOURCES})

    target_include_directories(td-engine PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # Emit a .js glue file (Emscripten convention) into web/.
    set_target_properties(td-engine PROPERTIES
        OUTPUT_NAME "td-engine"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
    )

    # Emscripten compile + link flags. Must match the Makefile's EMCCFLAGS.
    target_compile_options(td-engine PRIVATE
        -s WASM=1
        -s USE_WEBGL2=1
        -s ALLOW_MEMORY_GROWTH=1
        -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1
    )

    target_link_options(td-engine PRIVATE
        -s WASM=1
        -s USE_WEBGL2=1
        -s ALLOW_MEMORY_GROWTH=1
        -s NO_EXIT_RUNTIME=1
        -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1
        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32","HEAP16","HEAPU8","getValue","setValue","addFunction"]'
        -s ALLOW_TABLE_GROWTH=1
        -s EXPORTED_FUNCTIONS='["_main","_malloc","_free","_td_init","_td_shutdown","_td_load_scene","_td_set_key_state","_td_set_mouse_state","_td_resize","_td_get_version","_td_fill_audio_buffer","_td_create_entity","_td_entity_set_position","_td_entity_get_position","_td_entity_set_velocity","_td_entity_set_sprite","_td_entity_set_collider","_td_entity_destroy","_td_entity_is_valid","_td_get_entity_count","_td_is_key_down","_td_is_mouse_down","_td_get_mouse_pos","_td_render_frame","_td_set_callbacks","_td_beat_start","_td_beat_stop","_td_beat_is_on_beat","_td_beat_get_count","_td_beat_get_next_beat_time","_td_beat_get_last_beat_time","_td_beat_register_hit","_td_beat_get_combo","_td_beat_get_best_combo","_td_beat_reset_combo","_td_beat_set_callback","_td_beat_play_sound","_td_beat_set_bpm","_td_script_load","_td_script_call","_td_script_unload","_td_i18n_load","_td_i18n_set_locale","_td_i18n_t","_td_i18n_is_rtl","_td_touch_begin_frame","_td_touch_start","_td_touch_move","_td_touch_end","_td_touch_count","_td_touch_x","_td_touch_y","_td_touch_pinch_scale","_td_gamepad_begin_frame","_td_gamepad_set_connected","_td_gamepad_set_button","_td_gamepad_set_analog","_td_gamepad_set_axis","_td_gamepad_button_pressed","_td_gamepad_axis","_td_shader_graph_compile"]'
        -s ASYNCIFY=0
        -s EXIT_RUNTIME=0
    )

    # No desktop editor / examples / tests in the web build.
    set(TD_BUILD_EDITOR   OFF CACHE BOOL "" FORCE)
    set(TD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)

    message(STATUS "=============================")
    message(STATUS " TD Engine: WebAssembly build")
    message(STATUS "=============================")
    message(STATUS " Output: web/td-engine.js + web/td-engine.wasm")
    message(STATUS " Platform layer: wasm/emscripten_main.cpp")
    message(STATUS " Excluded (desktop-only): win32_window, win32_input, audio_engine, socket")
    message(STATUS "")

# =============================================================================
else()
# =============================================================================
# Desktop build (Win32 / Linux / macOS)
# =============================================================================
    set(PLATFORM_SOURCES
        src/platform/win32_window.cpp
        src/platform/win32_input.cpp
        src/audio/audio_engine.cpp
    )

    # Static library form of the engine - shared between editor, examples,
    # and tests so we don't recompile the engine N times.
    add_library(td-engine STATIC ${ENGINE_PORTABLE_SOURCES} ${PLATFORM_SOURCES})
    target_include_directories(td-engine PUBLIC ${CMAKE_SOURCE_DIR}/src)

    # find_package(Threads) is REQUIRED unconditionally because test_net
    # links `Threads::Threads` regardless of platform. On Windows / MSVC,
    # std::thread lives in the C++ runtime (vcruntime/libcpmt), so the
    # FindThreads module creates an empty IMPORTED target — no extra libs
    # are added. Calling this only inside `else()` (the previous setup)
    # left `Threads::Threads` undefined on Windows, breaking the MSVC
    # configure step (CMake Error at CMakeLists.txt:263).
    find_package(Threads REQUIRED)

    if(WIN32)
        target_link_libraries(td-engine PUBLIC
            Threads::Threads
            opengl32 gdi32 user32 winmm
        )
        target_compile_definitions(td-engine PUBLIC WIN32 _WINDOWS)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(td-engine PUBLIC
            Threads::Threads
            OpenGL::GL
        )
    endif()

    if(MSVC)
        target_compile_options(td-engine PRIVATE /W4)
    else()
        target_compile_options(td-engine PRIVATE -Wall -Wextra)
    endif()

    # --- Examples -----------------------------------------------------------
    if(TD_BUILD_EXAMPLES)
        add_executable(pong examples/pong/main.cpp)
        target_link_libraries(pong PRIVATE td-engine)

        add_executable(platformer examples/platformer/main.cpp)
        target_link_libraries(platformer PRIVATE td-engine)

        # Copy assets next to the executable so the examples can find them.
        add_custom_command(TARGET pong POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:pong>/assets
        )
        add_custom_command(TARGET platformer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:platformer>/assets
        )
    endif()

    # --- Tests --------------------------------------------------------------
    if(TD_BUILD_TESTS)
        enable_testing()
        add_executable(test_math  tests/test_math.cpp)
        target_link_libraries(test_math  PRIVATE td-engine)
        add_executable(test_ecs    tests/test_ecs.cpp)
        target_link_libraries(test_ecs    PRIVATE td-engine)
        add_executable(test_physics tests/test_physics.cpp)
        target_link_libraries(test_physics PRIVATE td-engine)
        # Regression test for the World constructor bug (memset(0) left
        # entity slots with id=0 instead of INVALID_ENTITY, which made
        # every createEntity() call fail with "Maximum entities reached").
        # Uses a stub logger so it builds without windows.h on Linux CI.
        add_executable(test_world_init tests/test_world_init.cpp tests/stub_logger.cpp)
        target_link_libraries(test_world_init PRIVATE td-engine)
        # Regression test for the component slot leak: removeComponent<T>()
        # used to leave the slot in the array and not decrement the count,
        # so addComponent<T>() would exhaust the array after TD_MAX_ENTITIES
        # add/remove cycles. Now uses swap-back pop to reclaim slots.
        add_executable(test_slot_leak tests/test_slot_leak.cpp tests/stub_logger.cpp)
        target_link_libraries(test_slot_leak PRIVATE td-engine)
        # UI toolkit tests (wave1-ui): layout, hit-testing, input dispatch,
        # virtualized ListView, style inheritance. Uses stub_logger.cpp for
        # Logger symbols and stub_gl.cpp for the `GLFunctions gl` global so
        # it builds on Linux without windows.h. The test never calls GL.
        add_executable(test_ui tests/test_ui.cpp tests/stub_logger.cpp tests/stub_gl.cpp)
        target_link_libraries(test_ui PRIVATE td-engine)
        # Voxel system tests (wave1-voxel): meshers (naive/greedy/culled),
        # ambient occlusion, worldgen, raycast, VoxelWorld get/set, frustum
        # culling, chunk streaming, lighting. Compiled directly (NOT linked
        # against td-engine) so it builds on Linux without the GL renderer
        # (which #includes <windows.h> on desktop). Uses stub_logger.cpp for
        # Logger symbols and stub_mesh.cpp for Mesh symbols — the test reads
        # VoxelMesh vertices/indices directly and never calls GL.
        add_executable(test_voxel
            tests/test_voxel.cpp
            src/voxel/voxel.cpp
            tests/stub_logger.cpp
            tests/stub_mesh.cpp)
        target_include_directories(test_voxel PRIVATE ${CMAKE_SOURCE_DIR}/src)
        # Networking tests (wave1-net): reliable UDP transport, RPC,
        # fragmentation, client prediction + server reconciliation.
        # Linked directly against the .cpp (not td-engine) so it builds on
        # Linux without the GL renderer. Uses stub_logger.cpp for Logger.
        add_executable(test_net
            tests/test_net.cpp
            src/net/transport.cpp
            src/net/server_authoritative.cpp
            tests/stub_logger.cpp)
        target_include_directories(test_net PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(test_net PRIVATE Threads::Threads)
        # 3D character controller tests (wave1-physaudio): swept capsule
        # collision, sliding, slope limit, step handling, ground detection.
        add_executable(test_character_controller
            tests/test_character_controller.cpp
            src/physics/character_controller.cpp
            src/physics/aabb.cpp
            src/physics/collision.cpp
            tests/stub_logger.cpp)
        target_include_directories(test_character_controller PRIVATE ${CMAKE_SOURCE_DIR}/src)
        add_test(NAME math COMMAND test_math)
        add_test(NAME ecs   COMMAND test_ecs)
        add_test(NAME physics COMMAND test_physics)
        add_test(NAME world_init COMMAND test_world_init)
        add_test(NAME slot_leak COMMAND test_slot_leak)
        add_test(NAME ui    COMMAND test_ui)
        add_test(NAME voxel COMMAND test_voxel)
        add_test(NAME net   COMMAND test_net)
        add_test(NAME character_controller COMMAND test_character_controller)
    endif()

    # --- Editor -------------------------------------------------------------
    if(TD_BUILD_EDITOR AND EXISTS "${CMAKE_SOURCE_DIR}/editor/main.cpp")
        file(GLOB EDITOR_SOURCES "editor/*.cpp")
        add_executable(td-editor ${EDITOR_SOURCES})
        target_link_libraries(td-editor PRIVATE td-engine)
        set_target_properties(td-editor PROPERTIES
            OUTPUT_NAME "td-editor"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()

    message(STATUS "============================")
    message(STATUS " TD Engine: Desktop build")
    message(STATUS "============================")
    message(STATUS " Platform layer: ${PLATFORM_SOURCES}")
    message(STATUS " Editor:   $<IF:$<BOOL:${TD_BUILD_EDITOR}>,yes,no>")
    message(STATUS " Examples: $<IF:$<BOOL:${TD_BUILD_EXAMPLES}>,yes,no>")
    message(STATUS " Tests:    $<IF:$<BOOL:${TD_BUILD_TESTS}>,yes,no>")
    message(STATUS "")

endif()

# =============================================================================
# Install rules (desktop only)
# =============================================================================
if(NOT TD_BUILD_WEB)
    install(TARGETS td-engine ARCHIVE DESTINATION lib)
    install(DIRECTORY src/ DESTINATION include/td-engine
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY assets/ DESTINATION share/td-engine/assets)
endif()
