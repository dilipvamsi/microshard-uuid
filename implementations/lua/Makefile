# Makefile for MicroShardUUID Lua Library

# Detect interpreter: Try LuaJIT first, then lua, then lua5.1
LUA := $(shell command -v luajit 2> /dev/null)
ifndef LUA
    LUA := $(shell command -v lua 2> /dev/null)
endif
ifndef LUA
    LUA := $(shell command -v lua5.1 2> /dev/null)
endif

.PHONY: test clean help

help:
	@echo "Available commands:"
	@echo "  make test   - Run the test suite"
	@echo "  make clean  - Remove temporary files (if any)"

test:
	@if [ -z "$(LUA)" ]; then \
		echo "Error: No Lua interpreter found (lua, luajit, or lua5.1)."; \
		exit 1; \
	fi
	@echo "Running tests using: $(LUA)"
	@$(LUA) test_microshard.lua

clean:
	@echo "Cleaning up..."
	@rm -f *.out
