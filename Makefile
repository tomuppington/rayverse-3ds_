# Rayverse 3DS build file
# (uses '>' instead of tabs so it survives copy/paste into GitHub's web editor)
.RECIPEPREFIX = >

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif

TARGET      := rayverse3ds
APP_TITLE   := Rayverse 3DS
APP_DESC    := Rayman 1 PC port (needs your own game data)
APP_AUTHOR  := Rayverse team / 3DS port
APP_ICON    := $(DEVKITPRO)/libctru/default_icon.png

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
# -fsigned-char: the original game was written for PCs, where 'char' is signed. On ARM it is
# unsigned by default, which would break things like negative speeds.
CFLAGS  := -g -O2 -std=gnu11 -fsigned-char -mword-relocations -ffunction-sections -fdata-sections \
           $(ARCH) -D__3DS__ -I$(DEVKITPRO)/libctru/include \
           -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map -L$(DEVKITPRO)/libctru/lib
LIBS    := -lctru -lm

# Rayverse is a "unity build": rayverse.c #includes all the other game files.
# stb_vorbis.c (the OGG music decoder) is compiled on its own, like in the original CMakeLists.txt.
SRC     := src/rayverse.c src/stb_vorbis.c
DEPS    := $(wildcard src/*.c src/*.h)

all: $(TARGET).3dsx

$(TARGET).elf: $(DEPS)
> $(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).smdh: $(APP_ICON)
> $(DEVKITPRO)/tools/bin/smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
> $(DEVKITPRO)/tools/bin/3dsxtool $< $@ --smdh=$(TARGET).smdh

clean:
> rm -f $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).map

.PHONY: all clean
