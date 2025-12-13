# ==============================================================================
# Settings
# ==============================================================================

# Virtual environment directory
VENV_DIR := .venv

# Binaries inside the venv
PYTHON := $(VENV_DIR)/bin/python
PIP := $(VENV_DIR)/bin/pip

# Test target
TEST_FILE := tests/test_microshard.py

# ==============================================================================
# Targets
# ==============================================================================

.PHONY: all
all: test

# 1. Create Virtual Environment (only if directory doesn't exist)
$(VENV_DIR):
	@echo "--- Creating Python virtual environment in $(VENV_DIR) ---"
	python3 -m venv $(VENV_DIR)

# 2. Install Dependencies (postgres)
.PHONY: install
install: $(VENV_DIR)
	@echo "--- Installing Postgres Dependencies ---"
	$(PIP) install --upgrade pip
	$(PIP) install psycopg2-binary

# 3. Run Tests
.PHONY: test
test: install
	@echo "--- Running Tests ---"
	$(PYTHON) -m unittest $(TEST_FILE)

# 4. Clean Up
.PHONY: clean
clean:
	@echo "--- Cleaning up ---"
	rm -rf $(VENV_DIR)
	find . -type f -name "*.pyc" -delete
	find . -type d -name "__pycache__" -delete

# Postgres Docker for testing
docker:
	docker run -p 5432:5432 --name test-microshard-postgres -e POSTGRES_PASSWORD=postgres postgres
