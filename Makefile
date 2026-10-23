LLVM_CONFIG ?= $(shell command -v llvm-config 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/llvm-config)
export LLVM_CONFIG

CXX      ?= clang++
CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags) -fexceptions -O2 -g -Wall -Wextra -Wno-unused-parameter
LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags) -Wl,-rpath,$(shell $(LLVM_CONFIG) --libdir)
LIBS     := $(shell $(LLVM_CONFIG) --libs core passes target all-targets) $(shell $(LLVM_CONFIG) --system-libs)

SRCS := $(wildcard src/*.cpp)
OBJS := $(SRCS:src/%.cpp=build/%.o)

.PHONY: all test test-linux bench clean web

all: build/mccomp

build/mccomp: $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

build/%.o: src/%.cpp src/*.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

test: build/mccomp
	./tests/run_tests.sh

test-linux: build/mccomp
	./tests/run_linux_docker.sh

bench: build/mccomp
	./bench/run.sh

web: build/mccomp
	node web/server.mjs

clean:
	rm -rf build output.ll
