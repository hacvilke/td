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
ENGINE_SRC = \
    $(wildcard $(SRC_DIR)/core/*.cpp) \
    $(wildcard $(SRC_DIR)/platform/*.cpp) \
    $(wildcard $(SRC_DIR)/renderer/*.cpp) \
    $(wildcard $(SRC_DIR)/physics/*.cpp) \
    $(wildcard $(SRC_DIR)/audio/*.cpp) \
    $(wildcard $(SRC_DIR)/net/*.cpp) \
    $(wildcard $(SRC_DIR)/assets/*.cpp) \
    $(wildcard $(SRC_DIR)/ecs/*.cpp) \
    $(wildcard $(SRC_DIR)/td/*.cpp)

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
#   - src/net/socket.cpp               (uses Winsock; JS WebSocket replaces it)
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
    $(SRC_DIR)/td/lexer.cpp \
    $(SRC_DIR)/td/parser.cpp \
    $(SRC_DIR)/td/compiler.cpp \
    $(SRC_DIR)/td/vm.cpp

WASM_MAIN_SRC = $(WASM_DIR)/emscripten_main.cpp

EMCCFLAGS ?= -O2 -std=c++17 -Wall \
             -s WASM=1 \
             -s USE_WEBGL2=1 \
             -s ALLOW_MEMORY_GROWTH=1 \
             -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1 \
             -s NO_EXIT_RUNTIME=1 \
             -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","HEAPF32","HEAP16","HEAPU8","getValue","setValue"]' \
             -s EXPORTED_FUNCTIONS='["_main","_malloc","_free","_td_init","_td_shutdown","_td_load_scene","_td_set_key_state","_td_set_mouse_state","_td_resize","_td_get_version","_td_fill_audio_buffer","_td_create_entity","_td_entity_set_position","_td_entity_get_position","_td_entity_set_velocity","_td_entity_set_sprite","_td_entity_set_collider","_td_entity_destroy","_td_entity_is_valid","_td_get_entity_count","_td_is_key_down","_td_is_mouse_down","_td_get_mouse_pos","_td_render_frame","_td_set_callbacks"]' \
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
	@echo ""
	@echo "WebAssembly build complete:"
	@echo "  $(WEB_DIR)/td-engine.js   (emcc glue)"
	@echo "  $(WEB_DIR)/td-engine.wasm (binary)"
	@echo ""
	@echo "To run locally:"
	@echo "  cd web && python3 -m http.server 8000"
	@echo "  # then open http://localhost:8000 in a browser"

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
