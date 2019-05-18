CC=gcc
CCFLAGS=-shared -fPIC
FILES=doorstop.c

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	EXT = so
	CCFLAGS += -D LINUX
endif
ifeq ($(UNAME_S), Darwin)
	EXT = dylib
	CCFLAGS += -D OSX
endif

build: build_x86 build_x64

build_x86: $(DEPS)
	$(CC) -o libdoorstop_x86.$(EXT) $(CCFLAGS) -D IA32 -m32 $(FILES)

build_x64: $(DEPS)
	$(CC) -o libdoorstop_x64.$(EXT) $(CCFLAGS) -D AMD64 -m64 $(FILES)