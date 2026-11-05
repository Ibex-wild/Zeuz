# Makefile for Zeuz AOT compiler (LLVM backend)

# Use the versioned llvm-config if available, else default
LLVM_CONFIG := /usr/lib/llvm/22/bin/llvm-config

# If llvm-config is not in PATH, try /usr/lib/llvm/22/bin/llvm-config
ifeq ($(shell which $(LLVM_CONFIG) 2>/dev/null),)
  LLVM_CONFIG := /usr/lib/llvm/22/bin/llvm-config
endif

ifeq ($(shell test -x $(LLVM_CONFIG) && echo yes),)
  $(error llvm-config not found. Install llvm-core/llvm or set LLVM_CONFIG)
endif

CXX      ?= clang++
CXXSTD   ?= -std=c++17
OPT      ?= -O2

LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBDIR   := $(shell $(LLVM_CONFIG) --libdir)
LLVM_INCDIR   := $(shell $(LLVM_CONFIG) --includedir)

LLVM_LIBFILES := $(shell $(LLVM_CONFIG) --libfiles)
LLVM_SYSLIBS  := $(shell $(LLVM_CONFIG) --system-libs)

CXXFLAGS := $(CXXSTD) $(OPT) -Wall -Wextra -Wno-unused-parameter $(LLVM_CXXFLAGS) -I$(LLVM_INCDIR)
LDFLAGS  := $(LLVM_LDFLAGS) -L$(LLVM_LIBDIR)
LDLIBS   := $(LLVM_LIBFILES) $(LLVM_SYSLIBS) -lm

SRCS := main.cpp lexer.cpp parser.cpp semantic.cpp codegen.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := zeuz

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

print-flags:
	@echo "LLVM_CONFIG = $(LLVM_CONFIG)"
	@echo "CXXFLAGS    = $(CXXFLAGS)"
	@echo "LDFLAGS     = $(LDFLAGS)"
	@echo "LDLIBS      = $(LDLIBS)"
