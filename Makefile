# Library metadata.
TARGET       := libGoldHEN_Hook.prx
TARGETSTUB   := libGoldHEN_Hook_Stub.so
TARGETSTATIC := libGoldHEN_Hook.a
TARGETCRT    := build/crtprx.o

# Libraries linked into the ELF.
LIBS         := -lSceLibcInternal -lkernel -lSceSysmodule

LOG_TYPE = -D__USE_KLOG__
DEBUG_FLAGS = -DDEBUG=0

ifeq ($(PRINTF),1)
    LOG_TYPE = -D__USE_PRINTF__
endif

ifeq ($(DEBUGFLAGS),1)
    DEBUG_FLAGS = -DDEBUG=1
endif

# Additional compile flags.
EXTRAFLAGS  := $(DEBUG_FLAGS) $(LOG_TYPE)

# Root vars
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJDIR     := source
INTDIR      := build
INCLUDEDIR  := include
DEBUGFLAGS  := 0

# Define objects to build
CFILES      := $(wildcard $(PROJDIR)/*.c)
CPPFILES    := $(wildcard $(PROJDIR)/*.cpp)
COMMONFILES := $(wildcard $(COMMONDIR)/*.cpp)
OBJS        := $(patsubst $(PROJDIR)/%.c, $(INTDIR)/%.o, $(CFILES)) $(patsubst $(PROJDIR)/%.cpp, $(INTDIR)/%.o, $(CPPFILES)) $(patsubst $(COMMONDIR)/%.cpp, $(INTDIR)/%.o, $(COMMONFILES))
STUBOBJS    := $(patsubst $(PROJDIR)/%.c, $(INTDIR)/%.o, $(CFILES)) $(patsubst $(PROJDIR)/%.cpp, $(INTDIR)/%.o.stub, $(CPPFILES)) $(patsubst $(COMMONDIR)/%.cpp, $(INTDIR)/%.o.stub, $(COMMONFILES))

# Define final C/C++ flags
CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -Iinclude
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/$(INCLUDEDIR)/c++/v1
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x -e _init --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS)

# Create the intermediate directory incase it doesn't already exist.
_unused     := $(shell mkdir -p $(INTDIR))

# Check for linux vs macOS and account for clang/ld path
UNAME_S     := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
		CC      := clang
		CCX     := clang++
		LD      := ld.lld
		CDIR    := linux
		AR      := llvm-ar
endif
ifeq ($(UNAME_S),Darwin)
		CC      := /usr/local/opt/llvm/bin/clang
		CCX     := /usr/local/opt/llvm/bin/clang++
		LD      := /usr/local/opt/llvm/bin/ld.lld
		AR      := /usr/local/opt/llvm/bin/llvm-ar
		CDIR    := macos
endif

$(TARGET): $(INTDIR) $(OBJS)
	$(LD) $(INTDIR)/*.o $(TARGETCRT) -o $(INTDIR)/$(PROJDIR).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/$(PROJDIR).elf -out=$(INTDIR)/$(PROJDIR).oelf --lib=$(TARGET) --paid 0x3800000000000011

$(TARGETSTATIC): $(INTDIR) $(OBJS)
	$(AR) --format=bsd rcs $(TARGETSTATIC) $(TARGETCRT) $(INTDIR)/*.o

$(TARGETSTUB): $(INTDIR) $(STUBOBJS)
	$(CC) $(INTDIR)/*.o.stub -o $(TARGETSTUB) -target x86_64-pc-linux-gnu -shared -fuse-ld=lld -ffreestanding -nostdlib -fno-builtin -L$(TOOLCHAIN)/lib $(LIBS)

$(INTDIR)/%.o: $(PROJDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/%.o: $(PROJDIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<

$(INTDIR)/%.o.stub: $(PROJDIR)/%.c
	$(CC) -target x86_64-pc-linux-gnu -ffreestanding -nostdlib -fno-builtin -fPIC -s -c -o $@ $<

$(INTDIR)/%.o.stub: $(PROJDIR)/%.cpp
	$(CCX) -target x86_64-pc-linux-gnu -ffreestanding -nostdlib -fno-builtin -fPIC -s -c -o $@ $<

.PHONY: clean crt
.DEFAULT_GOAL := all

all: clean crt $(TARGETSTATIC)

clean:
	rm -rf $(TARGET) $(TARGETSTUB) $(INTDIR) $(OBJS) $(TARGETCRT) $(TARGETSTATIC)

crt:
	@mkdir -p build
	$(CC) -target x86_64-pc-linux-gnu -ffreestanding -nostdlib -fno-builtin -fPIC -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -c crt/crtprx.c -o $(TARGETCRT)

install: all
	@echo Copying...
	@mkdir -p $(OO_PS4_TOOLCHAIN)/include/GoldHEN
	@cp -frv include/* $(OO_PS4_TOOLCHAIN)/include/GoldHEN
	@cp -frv $(TARGETSTATIC) $(OO_PS4_TOOLCHAIN)/lib
	@echo Done!
