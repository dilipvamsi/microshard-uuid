# Detect OS
UNAME_S := $(shell uname -s)

CC = gcc
# -O2 for optimization, -fPIC for position independent code
CFLAGS = -g -fPIC -O2

# Default target
all: microshard_uuid

microshard_uuid: microshard_uuid.c
	@echo "Detected OS: $(UNAME_S)"

# Linux
ifeq ($(UNAME_S),Linux)
	$(CC) $(CFLAGS) -shared microshard_uuid.c -o microshard_uuid.so
endif

# macOS
ifeq ($(UNAME_S),Darwin)
	$(CC) $(CFLAGS) -dynamiclib microshard_uuid.c -o microshard_uuid.dylib
endif

# Windows (MinGW / MSYS2 / Git Bash)
# We use 'findstring' because uname returns things like "MINGW64_NT-10.0-22631"
ifneq (,$(findstring MINGW,$(UNAME_S)))
	$(CC) $(CFLAGS) -shared microshard_uuid.c -o microshard_uuid.dll
endif

ifneq (,$(findstring MSYS,$(UNAME_S)))
	$(CC) $(CFLAGS) -shared microshard_uuid.c -o microshard_uuid.dll
endif

ifneq (,$(findstring CYGWIN,$(UNAME_S)))
	$(CC) $(CFLAGS) -shared microshard_uuid.c -o microshard_uuid.dll
endif

clean:
	rm -f *.so *.dylib *.dll
