#!/usr/bin/env python3
"""Patch the Makefile to add the TDScript compiler test target. Preserves tabs."""
import sys

MK_PATH = '/home/z/my-project/td-gh/Makefile'

with open(MK_PATH, 'r') as f:
    content = f.read()

T = '\t'  # tab character

# Add TDSCRIPT_TEST_SRC after the NET_TEST_SRC block
old_block = (
    "NET_TEST_SRC := \\\n"
    f"{T}$(SRC_DIR)/net/transport.cpp \\\n"
    f"{T}$(SRC_DIR)/net/server_authoritative.cpp \\\n"
    f"{T}$(SRC_DIR)/net/json_rpc.cpp\n"
    "\n"
    "# Per-test compile recipe."
)

new_block = (
    "NET_TEST_SRC := \\\n"
    f"{T}$(SRC_DIR)/net/transport.cpp \\\n"
    f"{T}$(SRC_DIR)/net/server_authoritative.cpp \\\n"
    f"{T}$(SRC_DIR)/net/json_rpc.cpp\n"
    "\n"
    "# Sources for the TDScript compiler test (lexer + parser + codegen).\n"
    "TDSCRIPT_TEST_SRC := \\\n"
    f"{T}$(SRC_DIR)/scripting/tdscript/lexer.cpp \\\n"
    f"{T}$(SRC_DIR)/scripting/tdscript/parser.cpp \\\n"
    f"{T}$(SRC_DIR)/scripting/tdscript/codegen_js.cpp \\\n"
    f"{T}$(SRC_DIR)/scripting/tdscript/tdscript.cpp\n"
    "\n"
    "# Per-test compile recipe."
)

if old_block not in content:
    print("ERROR: old_block not found", file=sys.stderr)
    sys.exit(1)
content = content.replace(old_block, new_block)

# Add test_tdscript_compiler target + update the test runner
old_recipe = (
    "$(TEST_BIN_DIR)/test_net: tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp\n"
    f"{T}@mkdir -p $(TEST_BIN_DIR)\n"
    f"{T}$(CXX) -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR) -DTEST_STUB_LOGGER \\\n"
    f"{T}        tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp \\\n"
    f"{T}        -lpthread -o $@\n"
    "\n"
    "# Run all C++ tests. Exits non-zero if any test fails.\n"
    "test: $(TEST_BIN_DIR)/test_net $(TEST_BIN_DIR)/test_net_json_rpc\n"
    f"{T}@echo \"Running C++ tests...\"\n"
    f"{T}@$(TEST_BIN_DIR)/test_net; r1=$$?; \\\n"
    f"{T} $(TEST_BIN_DIR)/test_net_json_rpc; r2=$$?; \\\n"
    f"{T} if [ $$r1 -eq 0 ] && [ $$r2 -eq 0 ]; then \\\n"
    f"{T}   echo \"All C++ tests passed.\"; \\\n"
    f"{T} else \\\n"
    f"{T}   echo \"Some C++ tests failed (net=$$r1, json_rpc=$$r2).\"; \\\n"
    f"{T}   exit 1; \\\n"
    f"{T} fi\n"
    "\n"
    ".PHONY: test"
)

new_recipe = (
    "$(TEST_BIN_DIR)/test_net: tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp\n"
    f"{T}@mkdir -p $(TEST_BIN_DIR)\n"
    f"{T}$(CXX) -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR) -DTEST_STUB_LOGGER \\\n"
    f"{T}        tests/test_net.cpp $(NET_TEST_SRC) tests/stub_logger.cpp \\\n"
    f"{T}        -lpthread -o $@\n"
    "\n"
    "$(TEST_BIN_DIR)/test_tdscript_compiler: tests/test_tdscript_compiler.cpp $(TDSCRIPT_TEST_SRC)\n"
    f"{T}@mkdir -p $(TEST_BIN_DIR)\n"
    f"{T}$(CXX) -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR) \\\n"
    f"{T}        tests/test_tdscript_compiler.cpp $(TDSCRIPT_TEST_SRC) \\\n"
    f"{T}        -lpthread -o $@\n"
    "\n"
    "# Run all C++ tests. Exits non-zero if any test fails.\n"
    "test: $(TEST_BIN_DIR)/test_net $(TEST_BIN_DIR)/test_net_json_rpc $(TEST_BIN_DIR)/test_tdscript_compiler\n"
    f"{T}@echo \"Running C++ tests...\"\n"
    f"{T}@$(TEST_BIN_DIR)/test_net; r1=$$?; \\\n"
    f"{T} $(TEST_BIN_DIR)/test_net_json_rpc; r2=$$?; \\\n"
    f"{T} $(TEST_BIN_DIR)/test_tdscript_compiler; r3=$$?; \\\n"
    f"{T} if [ $$r1 -eq 0 ] && [ $$r2 -eq 0 ] && [ $$r3 -eq 0 ]; then \\\n"
    f"{T}   echo \"All C++ tests passed.\"; \\\n"
    f"{T} else \\\n"
    f"{T}   echo \"Some C++ tests failed (net=$$r1, json_rpc=$$r2, tdscript=$$r3).\"; \\\n"
    f"{T}   exit 1; \\\n"
    f"{T} fi\n"
    "\n"
    ".PHONY: test"
)

if old_recipe not in content:
    print("ERROR: old_recipe not found", file=sys.stderr)
    sys.exit(1)
content = content.replace(old_recipe, new_recipe)

with open(MK_PATH, 'w') as f:
    f.write(content)

print("Makefile patched successfully")
