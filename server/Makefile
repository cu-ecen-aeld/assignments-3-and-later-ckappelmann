SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CFLAGS = -g -Wall -Werror
CC ?= gcc
LDFLAGS ?= -pthread -lrt
USE_AESD_CHAR_DEVICE ?= 1
ifeq ($(USE_AESD_CHAR_DEVICE),1)
CFLAGS += -DUSE_AESD_CHAR_DEVICE
endif

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-rm -f *.o $(TARGET) *.elf *.map
