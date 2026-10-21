# Rayverse 3DS build file (written so it still works if tabs get lost when pasting)
.RECIPEPREFIX = >

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif

TARGET  := rayverse3ds
CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -I$(DEVKITPRO)/libctru/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(DEVKITPRO)/libctru/lib
LIBS    := -lctru -lm
SRC     := $(wildcard source/*.c)

all: $(TARGET).3dsx

$(TARGET).elf: $(SRC)
> $(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).3dsx: $(TARGET).elf
> $(DEVKITPRO)/tools/bin/3dsxtool $< $@

clean:
> rm -f $(TARGET).elf $(TARGET).3dsx

.PHONY: all clean
