# device specific naming and paths
DEVICE=same51j20a
CPU=Cortex-M4
BOARD=microchip_same51_curiosity_nano
PACK=E51-pack

# standard compiling setup
CC=arm-none-eabi-gcc
DEVICE_UPPER=$(shell echo $(DEVICE) | tr  '[:lower:]' '[:upper:]')

INCLUDE_PATHS=-I../../$(PACK)/include -I../../Core/Include -I../include
ASFLAGS=-mthumb -mcpu=$(CPU) -D__$(DEVICE_UPPER)__ -O0 -ffunction-sections -Wall
CFLAGS=-x c -mthumb -mcpu=$(CPU) -D__$(DEVICE_UPPER)__ -O0 -ffunction-sections -Wall -c -std=gnu99

HDRS = $(wildcard *.h) $(wildcard ../include/*.h)
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o) $(wildcard ../drivers/*.o)

all: $(OBJS)

debug: CFLAGS += -g
debug: all
release: CFLAGS += -DNDEBUG
release: all

.PRECIOUS: %.o

# build all sources needed by the project
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) $*.c -o $*.o

# build the executable file
%.elf: %.o $(OBJS) $(SYS_OBJS)
	$(CC) -o $*.elf $(LDFLAGS) $(OBJS) $(SYS_OBJS)
	arm-none-eabi-objcopy -O ihex -R .eeprom -R .fuse -R .lock -R .signature  $*.elf $*.hex
	arm-none-eabi-objdump -h -S $*.elf > $*.lss
	arm-none-eabi-size $*.elf

clean:
	rm -f *.o
