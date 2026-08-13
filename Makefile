# TD Engine Makefile
# Supports both MinGW and MSVC

# Detect compiler
ifdef MSVC
    CXX = cl
    CC = cl
    AR = lib
    CXXFLAGS = /std:c++17 /W4 /O2 /EHsc /MD /DWIN32 /D_WINDOWS
    DEBUG_FLAGS = /Od /Zi /DDEBUG /MDd
    OUT_OBJ = /Fo
    OUT_EXE = /Fe
    OUT_LIB = /OUT:
    LIBS = opengl32.lib gdi32.lib user32.lib winmm.lib ws2_32.lib
else
    CXX = g++
    CC = gcc
    AR = ar
    CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -DWIN32
    DEBUG_FLAGS = -g -O0 -DDEBUG
    OUT_OBJ = -o 
    OUT_EXE = -o 
    OUT_LIB = 
    LIBS = -lopengl32 -lgdi32 -luser32 -lwinmm -lws2_32
endif

# Directories
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
LIB_DIR = $(BUILD_DIR)/lib

# Source files
CORE_SRC = $(wildcard $(SRC_DIR)/core/*.cpp) $(wildcard $(SRC_DIR)/core/math/*.cpp)
PLATFORM_SRC = $(wildcard $(SRC_DIR)/platform/*.cpp)
RENDERER_SRC = $(wildcard $(SRC_DIR)/renderer/*.cpp)
PHYSICS_SRC = $(wildcard $(SRC_DIR)/physics/*.cpp)
AUDIO_SRC = $(wildcard $(SRC_DIR)/audio/*.cpp)
NET_SRC = $(wildcard $(SRC_DIR)/net/*.cpp)
ASSETS_SRC = $(wildcard $(SRC_DIR)/assets/*.cpp)
ECS_SRC = $(wildcard $(SRC_DIR)/ecs/*.cpp)

ENGINE_SRC = $(CORE_SRC) $(PLATFORM_SRC) $(RENDERER_SRC) $(PHYSICS_SRC) \
             $(AUDIO_SRC) $(NET_SRC) $(ASSETS_SRC) $(ECS_SRC)

# Object files
ENGINE_OBJ = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(ENGINE_SRC))

# Targets
ENGINE_LIB = $(LIB_DIR)/td-engine.a
PONG_EXE = $(BIN_DIR)/pong.exe
PLATFORMER_EXE = $(BIN_DIR)/platformer.exe

# Default target
all: dirs $(ENGINE_LIB) $(PONG_EXE) $(PLATFORMER_EXE)

# Create directories
dirs:
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
	@if not exist $(OBJ_DIR)\core mkdir $(OBJ_DIR)\core
	@if not exist $(OBJ_DIR)\core\math mkdir $(OBJ_DIR)\core\math
	@if not exist $(OBJ_DIR)\platform mkdir $(OBJ_DIR)\platform
	@if not exist $(OBJ_DIR)\renderer mkdir $(OBJ_DIR)\renderer
	@if not exist $(OBJ_DIR)\physics mkdir $(OBJ_DIR)\physics
	@if not exist $(OBJ_DIR)\audio mkdir $(OBJ_DIR)\audio
	@if not exist $(OBJ_DIR)\net mkdir $(OBJ_DIR)\net
	@if not exist $(OBJ_DIR)\assets mkdir $(OBJ_DIR)\assets
	@if not exist $(OBJ_DIR)\ecs mkdir $(OBJ_DIR)\ecs
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)
	@if not exist $(LIB_DIR) mkdir $(LIB_DIR)

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c $< $(OUT_OBJ)$@

# Build engine static library
$(ENGINE_LIB): $(ENGINE_OBJ)
	$(AR) rcs $@ $^

# Build Pong example
$(PONG_EXE): examples/pong/main.cpp $(ENGINE_LIB)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(ENGINE_LIB) $(LIBS) $(OUT_EXE)$@

# Build Platformer example
$(PLATFORMER_EXE): examples/platformer/main.cpp $(ENGINE_LIB)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(ENGINE_LIB) $(LIBS) $(OUT_EXE)$@

# Debug build
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: all

# Clean
clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)

# Run examples
run-pong: $(PONG_EXE)
	$(PONG_EXE)

run-platformer: $(PLATFORMER_EXE)
	$(PLATFORMER_EXE)

# Phony targets
.PHONY: all dirs clean debug run-pong run-platformer
