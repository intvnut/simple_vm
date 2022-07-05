CXX ?= g++-11
CXXFLAGS ?= -std=c++17 -O3

all: vm

vm: vm.cc
	$(CXX) $(CXXFLAGS) -o vm vm.cc

orig_vm: orig_vm.cc
	$(CXX) $(CXXFLAGS) -o orig_vm orig_vm.cc
