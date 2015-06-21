BINARY       = snesgss-export

CXX         ?= g++
CXXFLAGS    ?= -lstdc++ -Wall -Wno-unused-variable -Wno-unused-function -Werror -O2 -g
LDFLAGS     ?= -g 

SOURCES     = $(wildcard src/*.cpp src/*/*.cpp)
HEADERS     = $(wildcard src/*.h src/*/*.h)
OBJECTS     = $(patsubst src/%.cpp,obj/%.o,$(SOURCES))
OBJECT_DIRS = $(sort $(dir $(OBJECTS)))

.PHONY: all
all: dirs $(BINARY)

$(BINARY): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJECTS): $(HEADERS) Makefile

.PHONY: dirs
dirs: $(OBJECT_DIRS)

$(OBJECT_DIRS):
	mkdir $@


.PHONY: clean
clean:
	$(RM) $(BINARY) $(OBJECTS)

