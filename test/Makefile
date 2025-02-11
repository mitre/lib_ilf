# Copyright (c) 2023 The MITRE Corporation.

CXX = clang++
CXXFLAGS = -Wall -g -std=c++11 -pthread -O2
INCLUDES = -I../

all: struct_to_ilf int_to_string

struct_to_ilf:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o struct_to_ilf struct_to_ilf.cpp

int_to_string:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o int_to_string int_to_string.cpp

clean:
	rm int_to_string string_to_ilf

