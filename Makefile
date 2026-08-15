# =============================================================================
# TD Engine - Makefile
#
# Two build targets:
#
#   make           - build the desktop engine + examples (default; MinGW / MSVC)
#   make web       - build the WebAssembly bundle (web/td-engine.js + .wasm)
#
# The desktop target compiles with the Win32 platform layer
# (src/platform/win32_*.cpp). The web target swaps that for
# wasm/emscripten_main.cpp and uses Emscripten's GLES3/WebGL2 backend.
#
# =============================================================================

# --- Toolchain ---------------------------------------------------------------
# Desktop: g++ (MinGW) by default. Override with `make CXX=clang++` if needed.
CXX      ?= g++
EMCC     ?= emcc
EMXX     ?= em++

# --- Directories -------------------------------------------------------------
BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj
BIN_DIR    := $(BUILD_DIR)/bin
LIB_DIR    := $(BUILD_DIR)/lib
WEB_DIR    := web
WASM_DIR   := wasm
SRC_DIR    := src

# =============================================================================
# Desktop build (Win32 / MinGW)
# =============================================================================
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -O2 -DWIN32 -I$(SRC_DIR)
WIN_LIBS  ?= -lopengl32 -lgdi32 -luser32 -lwinmm -lws2_32

# Engine sources (desktop = everything in src/, including platform/win32_*.cpp).
# Wildcards auto-pick up new .cpp files in any of these dirs. To add a new
# module, drop its .cpp files into the appropriate subdir and they get
# compiled. (Header-only modules like src/scene/, src/serialization/,
# src/net/ are #included by callers — no .cpp to build.)
# Note: src/ui/ graduated from header-only to a real .cpp (ui.cpp) in
# wave1-ui and is added explicitly to both ENGINE_SRC and WASM_ENGINE_SRC.
# src/voxel/ graduated from header-only to a real .cpp (voxel.cpp) in
# wave1-voxel and is added explicitly to both lists too.
ENGINE_SRC = \
        $(wildcard $(SRC_DIR)/core/*.cpp) \
        $(wildcard $(SRC_DIR)/platform/*.cpp) \
        $(wildcard $(SRC_DIR)/renderer/*.cpp) \
        $(wildcard $(SRC_DIR)/physics/*.cpp) \
        $(wildcard $(SRC_DIR)/audio/*.cpp) \
        $(wildcard $(SRC_DIR)/assets/*.cpp) \
        $(wildcard $(SRC_DIR)/ecs/*.cpp) \
        $(wildcard $(SRC_DIR)/scripting/*.cpp) \
        $(SRC_DIR)/ui/ui.cpp \
        $(SRC_DIR)/voxel/voxel.cpp

ENGINE_OBJ = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(ENGINE_SRC))

# Desktop targets.
ENGINE_LIB  = $(LIB_DIR)/td-engine.a
PONG_EXE    = $(BIN_DIR)/pong.exe
PLAT_EXE    = $(BIN_DIR)/platformer.exe
EDITOR_EXE  = $(BIN_DIR)/td-editor.exe

# =============================================================================
# WebAssembly build (Emscripten)
# =============================================================================
# Exclude desktop-only sources from the WASM build:
#   - src/platform/win32_window.cpp    (replaced by wasm/emscripten_main.cpp)
#   - src/platform/win32_input.cpp     (replaced by Emscripten HTML5 callbacks)
#   - src/audio/audio_engine.cpp       (uses waveOut; JS Web Audio replaces it)
# src/td/ (custom scripting VM) and src/net/ (Winsock) were removed in the
# engine cleanup pass — see docs/MODULARITY_ROADMAP.md for the plan to
# re-add them properly (Lua VM + ENet, Tier 1).
WASM_ENGINE_SRC = \
        $(SRC_DIR)/core/logger.cpp \
        $(SRC_DIR)/core/game_loop.cpp \
        $(SRC_DIR)/renderer/gl_renderer.cpp \
        $(SRC_DIR)/renderer/sprite_batch.cpp \
        $(SRC_DIR)/renderer/camera.cpp \
        $(SRC_DIR)/renderer/framebuffer.cpp \
        $(SRC_DIR)/renderer/gl_shader.cpp \
        $(SRC_DIR)/renderer/texture.cpp \
        $(SRC_DIR)/renderer/mesh.cpp \
        $(SRC_DIR)/physics/aabb.cpp \
        $(SRC_DIR)/physics/collision.cpp \
        $(SRC_DIR)/physics/rigidbody.cpp \
        $(SRC_DIR)/audio/wav_loader.cpp \
        $(SRC_DIR)/audio/mixer.cpp \
        $(SRC_DIR)/assets/png_decoder.cpp \
        $(SRC_DIR)/assets/obj_loader.cpp \
        $(SRC_DIR)/assets/asset_loader.cpp \
        $(SRC_DIR)/ecs/world.cpp \
        $(SRC_DIR)/ecs/entity.cpp \
        $(SRC_DIR)/ecs/beat_system.cpp \
        $(SRC_DIR)/scripting/script_vm.cpp \
        $(SRC_DIR)/ui/ui.cpp \
        $(SRC_DIR)/voxel/voxel.cpp

WASM_MAIN_SRC = $(WASM_DIR)/emscripten_main.cpp

EMCCFLAGS ?= -O2 -std=c++17 -Wall \
             -s WASM=1 \
             -s USE_WEBGL2=1 \
             -s ALLOW_MEMORY_GROWTH=1 \
             -s INITIAL_MEMORY=64MB \
             -s MAXIMUM_MEMORY=2GB \
             -s STACK_SIZE=8MB \
             -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1 \
             -s NO_EXIT_RUNTIME=1 \
             -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32","HEAP16","HEAPU8","getValue","setValue","addFunction"]' \
             -s ALLOW_TABLE_GROWTH=1 \
             -s EXPORTED_FUNCTIONS='["_main","_malloc","_free","_td_init","_td_shutdown","_td_load_scene","_td_set_key_state","_td_set_mouse_state","_td_resize","_td_get_version","_td_fill_audio_buffer","_td_create_entity","_td_entity_set_position","_td_entity_get_position","_td_entity_set_velocity","_td_entity_set_sprite","_td_entity_set_collider","_td_entity_destroy","_td_entity_is_valid","_td_get_entity_count","_td_is_key_down","_td_is_mouse_down","_td_get_mouse_pos","_td_render_frame","_td_set_callbacks","_td_beat_start","_td_beat_stop","_td_beat_is_on_beat","_td_beat_get_count","_td_beat_get_next_beat_time","_td_beat_get_last_beat_time","_td_beat_register_hit","_td_beat_get_combo","_td_beat_get_best_combo","_td_beat_reset_combo","_td_beat_set_callback","_td_beat_play_sound","_td_beat_set_bpm","_td_script_load","_td_script_call","_td_script_unload","_td_i18n_load","_td_i18n_set_locale","_td_i18n_t","_td_i18n_is_rtl","_td_touch_begin_frame","_td_touch_start","_td_touch_move","_td_touch_end","_td_touch_count","_td_touch_x","_td_touch_y","_td_touch_pinch_scale","_td_gamepad_begin_frame","_td_gamepad_set_connected","_td_gamepad_set_button","_td_gamepad_set_analog","_td_gamepad_set_axis","_td_gamepad_button_pressed","_td_gamepad_axis","_td_shader_graph_compile"]' \
             -I$(SRC_DIR)

# =============================================================================
# Targets
# =============================================================================

.PHONY: all web examples editor clean clean-web help

all: dirs $(ENGINE_LIB) $(PONG_EXE) $(PLAT_EXE)
	@echo "Desktop build complete: $(BIN_DIR)/"

# Create output directories (Windows mkdir syntax - works in MinGW cmd shell).
dirs:
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
	@if not exist $(LIB_DIR) mkdir $(LIB_DIR)
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

# --- Object file compilation -------------------------------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@if not exist $(dir $@) mkdir $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Engine static library ---------------------------------------------------
$(ENGINE_LIB): $(ENGINE_OBJ)
	$(AR) rcs $@ $^

# --- Pong example ------------------------------------------------------------
$(PONG_EXE): examples/pong/main.cpp $(ENGINE_LIB)
	$(CXX) $(CXXFLAGS) $< $(ENGINE_LIB) $(WIN_LIBS) -o $@

# --- Platformer example ------------------------------------------------------
$(PLAT_EXE): examples/platformer/main.cpp $(ENGINE_LIB)
	$(CXX) $(CXXFLAGS) $< $(ENGINE_LIB) $(WIN_LIBS) -o $@

# --- Editor (desktop) --------------------------------------------------------
editor: dirs $(ENGINE_LIB)
	@if exist editor\main.cpp \
	$(CXX) $(CXXFLAGS) $(wildcard editor/*.cpp) $(ENGINE_LIB) $(WIN_LIBS) -o $(EDITOR_EXE)

examples: $(PONG_EXE) $(PLAT_EXE)

# =============================================================================
# WebAssembly build
# =============================================================================
# Output: web/td-engine.js + web/td-engine.wasm (emcc generates both).
# The .js file is Emscripten's glue; wasm/js_bridge.js wraps it.
web: $(WEB_DIR)/td-engine.js

$(WEB_DIR)/td-engine.js: $(WASM_ENGINE_SRC) $(WASM_MAIN_SRC)
	@mkdir -p $(WEB_DIR)
	$(EMXX) $(EMCCFLAGS) $(WASM_ENGINE_SRC) $(WASM_MAIN_SRC) -o $(WEB_DIR)/td-engine.js
	@cp wasm/js_bridge.js $(WEB_DIR)/js_bridge.js
	@echo ""
	@echo "WebAssembly build complete:"
	@echo "  $(WEB_DIR)/td-engine.js   (emcc glue)"
	@echo "  $(WEB_DIR)/td-engine.wasm (binary)"
	@echo "  $(WEB_DIR)/js_bridge.js   (TDBridge, copied from wasm/)"
	@echo ""
	@echo "To run locally:"
	@echo "  cd web && python3 -m http.server 8000"
	@echo "  # then open http://localhost:8000 in a browser"

# =============================================================================
# Tests
# =============================================================================
# Builds + runs the C++ test executables. Cross-platform: detects whether
# the shell understands Unix or Windows commands and uses the right one.
#
# Linux/macOS: uses /bin/sh semantics (mkdir -p, rm -rf, $@).
# Windows (MinGW): uses cmd.exe semantics (if not exist, rmdir /s /q).
#
# The test binaries are placed in build/bin/ next to the engine lib so the
# `td test` CLI command can find them.
TEST_OBJ_DIR := $(BUILD_DIR)/obj/tests
TEST_BIN_DIR := $(BUILD_DIR)/bin

# Sources shared by the C++ net tests (transport + server_auth + json_rpc).
# These compile small and fast; we link them into both test_net and
# test_net_json_rpc.
NET_TEST_SRC := \
	$(SRC_DIR)/net/transport.cpp \
	$(SRC_DIR)/net/server_authoritative.cpp \
	$(SRC_DIR)/net/json_rpc.cpp

# Per-test compile recipe. -DTEST_STUB_LOGGER lets the test build without
# the real Logger (which #includes <windows.h> on desktop).
$(TEST_BIN_DIR)/test_net_json_rpc: tests/test_net_json_rpc.cpp $(NET_TEST_SRC) tests/stub_logger.cpp
	@mkdir -p $(TEST_BIN_DIR)
	$(CXX) -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR) -DTEST_STUB_LOGGER \
	        tests/test_net_json_rpc.cpp $(NET_TEST_SRC) tests/stub_logger.cpp \
	        -lpthread -o $@

$(TEST_BIN_DIR)/test_net: tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp
	@mkdir -p $(TEST_BIN_DIR)
	$(CXX) -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR) -DTEST_STUB_LOGGER \
	        tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp \
	        -lpthread -o $@

# Run all C++ tests. Exits non-zero if any test fails.
test: $(TEST_BIN_DIR)/test_net $(TEST_BIN_DIR)/test_net_json_rpc
	@echo "Running C++ tests..."
	@$(TEST_BIN_DIR)/test_net; r1=$$?; \
	 $(TEST_BIN_DIR)/test_net_json_rpc; r2=$$?; \
	 if [ $$r1 -eq 0 ] && [ $$r2 -eq 0 ]; then \
	   echo "All C++ tests passed."; \
	 else \
	   echo "Some C++ tests failed (net=$$r1, json_rpc=$$r2)."; \
	   exit 1; \
	 fi

.PHONY: test

# =============================================================================
# Clean
# =============================================================================
clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)

clean-web:
	@del /q $(WEB_DIR)\td-engine.js $(WEB_DIR)\td-engine.wasm 2>nul
	@del /q $(WEB_DIR)\td-engine.data 2>nul

# =============================================================================
# Help
# =============================================================================
help:
	@echo "TD Engine Makefile targets:"
	@echo ""
	@echo "Desktop (MinGW / MSVC):"
	@echo "  make           - build engine lib + pong + platformer (default)"
	@echo "  make editor    - build the desktop editor (if editor/*.cpp exist)"
	@echo "  make examples  - build pong + platformer"
	@echo "  make clean     - remove all desktop build artifacts"
	@echo ""
	@echo "WebAssembly (requires Emscripten on PATH):"
	@echo "  make web       - build web/td-engine.js + web/td-engine.wasm"
	@echo "  make clean-web - remove only the WASM bundle"
	@echo ""
	@echo "To run the web build:"
	@echo "  make web"
	@echo "  cd web"
	@echo "  python3 -m http.server 8000"
	@echo "  # open http://localhost:8000 in a browser"

# =============================================================================
# Phony / pattern rule
# =============================================================================
.PHONY: all dirs web examples editor clean clean-web help
