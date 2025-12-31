# ==============================================================================
# Settings
# ==============================================================================

# Virtual environment directory
VENV_DIR := .venv

# Binaries inside the venv
PYTHON := $(VENV_DIR)/bin/python
PIP := $(VENV_DIR)/bin/pip

# Test target
TEST_FILE := tests/test_microshard_uuid.py

.PHONY: all
all: test

# 1. Create Virtual Environment (only if directory doesn't exist)
$(VENV_DIR):
	@echo "--- Creating Python virtual environment in $(VENV_DIR) ---"
	python3 -m venv $(VENV_DIR)

# 2. Install Build Dependencies
.PHONY: build-deps
build-deps: $(VENV_DIR)
	@echo "--- Installing Build Dependencies ---"
	$(PIP) install --upgrade pip
	$(PIP) install build twine

# 3. Run Tests
.PHONY: test
test: $(VENV_DIR)
	@echo "--- Running Tests ---"
	PYTHONPATH="$(PYTHONPATH):./src" $(PYTHON) -m unittest $(TEST_FILE)

# 4. Clean Up
.PHONY: clean
clean:
	@echo "--- Cleaning up ---"
	rm -rf $(VENV_DIR)
	find . -type f -name "*.pyc" -delete
	find . -type d -name "__pycache__" -delete

# 5. Build Package
.PHONY: build
build: build-deps
	@echo "--- Building Package ---"
	$(PYTHON) -m build

# 5
.PHONY: publish
publish-check: build
		@echo "--- Publishing Package ---"
		$(PYTHON) -m twine check dist/*

# 6. Publish Package
.PHONY: publish
publish: publish-check
	@echo "--- Publishing Package ---"
	$(PYTHON) -m twine upload dist/*
