CXX = clang++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Wpedantic
SOURCES = $(wildcard src/*.cpp src/*/*.cpp)
HEADERS = $(wildcard src/*/*.h)
WORKERS ?= 4
CONNECTIONS ?= 100
SECONDS ?= 30
SIZE_MIB ?= 16
PORT ?= 8081

.PHONY: build test load1 load2
build: build/static_server build/connection_test

build/static_server: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

build/connection_test: load/connection_test.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -pthread $< -o $@

test: build
	python3 tests/smoke_test.py ./build/static_server

load1: build
	python3 scripts/run_load.py 1 --workers $(WORKERS) --connections $(CONNECTIONS) --seconds $(SECONDS) --size-mib $(SIZE_MIB) --port $(PORT)

load2: build
	python3 scripts/run_load.py 2 --workers $(WORKERS) --connections $(CONNECTIONS) --seconds $(SECONDS) --size-mib $(SIZE_MIB) --port $(PORT)
