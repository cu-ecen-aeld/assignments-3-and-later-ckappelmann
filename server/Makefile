SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CFLAGS = -g -Wall -Werror
CC ?= gcc
LDFLAGS ?= -pthread -lrt

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-rm -f *.o $(TARGET) *.elf *.map
