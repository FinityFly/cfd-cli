# Cross-platform Makefile for CFD simulation

# Detect operating system
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(shell uname -s)
endif

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -O2
LDFLAGS := -lm

# Windows-specific settings
ifeq ($(DETECTED_OS),Windows)
    EXE := .exe
    CFLAGS += -D_WIN32
    # If using MinGW, we might need additional flags
    ifeq ($(shell $(CC) -dumpmachine),mingw32)
        CFLAGS += -D__USE_MINGW_ANSI_STDIO=1
    endif
else
    EXE := 
    # Linux/macOS specific flags
    CFLAGS += -D_POSIX_C_SOURCE=200809L
    # macOS specific
    ifeq ($(DETECTED_OS),Darwin)
        CFLAGS += -D_DARWIN_C_SOURCE
    endif
endif

# Targets
TARGET := cfd$(EXE)
SRC := cfd.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

install: $(TARGET)
ifeq ($(DETECTED_OS),Windows)
	@echo "For Windows, please copy $(TARGET) to your desired location manually"
else
	@echo "Installing to /usr/local/bin"
	@sudo cp $(TARGET) /usr/local/bin/
	@sudo chmod +x /usr/local/bin/$(TARGET)
endif

uninstall:
ifeq ($(DETECTED_OS),Windows)
	@echo "For Windows, please remove $(TARGET) manually"
else
	@echo "Removing from /usr/local/bin"
	@sudo rm -f /usr/local/bin/$(TARGET)
endif

# Platform-specific helpers
info:
	@echo "Detected OS: $(DETECTED_OS)"
	@echo "Compiler: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "Target executable: $(TARGET)"
