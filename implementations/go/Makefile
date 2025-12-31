# Makefile for MicroShard UUID (Go Implementation)

# Variables
MODULE_PATH=github.com/dilipvamsi/microshard-uuid/implementations/go

# Phony targets
.PHONY: all build test fmt help install-go1.17 test-go1.17

# Default target
all: fmt test build

# --- Main Development Tasks ---

# Build: Checks compilation
build:
	go build -v ./...

# Run unit tests (Current Go Version)
test:
	go test -v ./...

# Format code
fmt:
	go fmt ./...

# --- Backward Compatibility (Go 1.17) ---

# Install Go 1.17 side-by-side
install-go1.17:
	@echo "Installing Go 1.17 wrapper..."
	go install golang.org/dl/go1.17@latest
	@echo "Downloading Go 1.17 SDK..."
	~/go/bin/go1.17 download
	@echo "Go 1.17 installed successfully!"

# Run tests specifically with Go 1.17
test-go1.17:
	@echo "Running tests with Go 1.17..."
	~/go/bin/go1.17 test -v ./...

# --- Help ---

help:
	@echo "MicroShard UUID (Go) Makefile"
	@echo "-----------------------------"
	@echo "make all         - Run fmt, vet, test, and build (Default)"
	@echo "make build       - Compile package"
	@echo "make test        - Run tests (Current Go)"
	@echo "make fmt         - Format code"
	@echo ""
	@echo "make publish     - Publish a new version (Requires VERSION=vX.Y.Z)"
	@echo "                   Example: make publish VERSION=v1.0.0"
	@echo ""
	@echo "Compatibility Testing:"
	@echo "make install-go1.17 - Install Go 1.17 side-by-side"
	@echo "make test-go1.17    - Run tests using Go 1.17"


# --- Publishing ---

# Usage: make publish VERSION=v1.0.0
publish:
	@if [ -z "$(VERSION)" ]; then \
		echo "❌ Error: VERSION is not set."; \
		echo "Usage: make publish VERSION=v1.0.0"; \
		exit 1; \
	fi
	@# Check if VERSION starts with 'v'
	@case "$(VERSION)" in \
		v*) ;; \
		*) echo "❌ Error: Version must start with 'v' (e.g., v1.0.0)"; exit 1 ;; \
	esac
	@echo "🚀 Tagging release implementations/go/$(VERSION)..."
	git tag implementations/go/$(VERSION)
	@echo "pushing tag to origin..."
	git push origin implementations/go/$(VERSION)
	@echo "🌐 Triggering Go Proxy..."
	GOPROXY=https://proxy.golang.org go list -m $(MODULE_PATH)@$(VERSION)
	@echo "✅ Published!"
