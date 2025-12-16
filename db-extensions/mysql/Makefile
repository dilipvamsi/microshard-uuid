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

# 2. Install Dependencies (mysql)
.PHONY: install
install: $(VENV_DIR)
	@echo "--- Installing MySQL Dependencies ---"
	$(PIP) install --upgrade pip
	# mysql-connector-python is the official Oracle driver
	# Alternative: pymysql (pure python)
	$(PIP) install mysql-connector-python

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

# MySQL Docker for testing
# -p 3306:3306 maps the standard MySQL port
# MYSQL_DATABASE creates a blank DB named 'microshard_test' on startup
docker:
	docker run -p 3306:3306 \
		--name test-microshard-mysql \
		-e MYSQL_ROOT_PASSWORD=root \
		-e MYSQL_DATABASE=microshard_test \
		mysql:latest
