CC ?= $(CROSS_COMPILE)gcc
FLAG ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
all: aesdsocket
aesdsocket: aesdsocket.c
	$(CC) -o aesdsocket aesdsocket.c $(FLAG) $(LDFLAGS)
clean:
	rm -f *.o aesdsocket
