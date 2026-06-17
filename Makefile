# srcterm — Source Engine developer-console terminal (C++ / Xlib / Xft / libvterm)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PKGS      = x11 xft fontconfig vterm
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS      = $(shell pkg-config --libs $(PKGS)) -lutil
PREFIX   ?= $(HOME)/.local

SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:.cpp=.o)
DEP = $(wildcard src/*.h)

srcterm: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LIBS)

# Every object depends on every header — the modules are small and tightly
# coupled through app.h, so a blanket dependency keeps rebuilds correct.
src/%.o: src/%.cpp $(DEP)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: srcterm
	./srcterm

install: srcterm
	ln -sf $(CURDIR)/srcterm $(PREFIX)/bin/srcterm

clean:
	rm -f srcterm src/*.o

.PHONY: run install clean
