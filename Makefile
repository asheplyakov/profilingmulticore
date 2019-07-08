

aligned_new := $(shell if $(CXX) -std=c++11 -faligned-new -o /dev/null src/test.cc 2>/dev/null; then echo OK; fi)
ifeq ($(strip $(aligned_new)),)
CXX := /opt/rh/devtoolset-7/root/usr/bin/g++
endif
CXXFLAGS ?= -O2 -g -Wall -pipe -fno-omit-frame-pointer
LDFLAGS ?=

all: bin/falsesharing bin/nomorefalsesharing


bin/falsesharing: .o/falsesharing.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/falsesharing.o: src/falsesharing.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread $<

bin/nomorefalsesharing: .o/nomorefalsesharing.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/nomorefalsesharing.o: src/nomorefalsesharing.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread -faligned-new $<

bench: bin/falsesharing
	./benchmark.sh

fixedbench: bin/falsesharing bin/nomorefalsesharing
	env SHOW_FIX=Y ./benchmark.sh


help:
	@echo Available targets
	@echo bench      compile and run false sharing demo
	@echo fixedbench compile and run fixed version

clean:
	@rm -rf .o
	@rm -rf bin
