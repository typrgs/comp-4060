# default target when "make" is run w/o arguments
all: main.elf

# device specific naming and paths
DEVICE=same51j20a
CPU=Cortex-M4
BOARD=microchip_same51_curiosity_nano
PACK=E51-pack

# standard compiling setup
CC=arm-none-eabi-gcc
DEVICE_UPPER=$(shell echo $(DEVICE) | tr  '[:lower:]' '[:upper:]')

INCLUDE_PATHS=-I../../../$(PACK)/include -I../../../Core/Include -I../include
ASFLAGS=-mthumb -mcpu=$(CPU) -D__$(DEVICE_UPPER)__ -O0 -ffunction-sections -Wall
CFLAGS=-x c -mthumb -mcpu=$(CPU) -D__$(DEVICE_UPPER)__ -O0 -ffunction-sections -Wall -c -std=gnu99
LDFLAGS=-Wl,--start-group -Wl,--end-group -Wl,--gc-sections -mthumb -mcpu=$(CPU) -T../../../$(PACK)/gcc/gcc/$(DEVICE)_flash.ld 

SYS_OBJS=../../../$(PACK)/gcc/gcc/startup_$(DEVICE).o

HDRS = $(wildcard *.h) $(wildcard ../include/*.h)
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o) $(wildcard ../drivers/*.o)

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
	rm -f *.o *.elf *.hex *.lss

# put the executable on the device
%-install: %.elf
	openocd -f board/$(BOARD).cfg -c "program $*.elf verify reset exit"
