.PHONY: all

CC = gcc
CFLAGS  = -Wall -Werror -m64
LDFLAGS =
RM = /bin/rm -f
COMMAND = rotator

all: rotator

clean:
	$(RM) *.o $(COMMAND)

OBJS = $(COMMAND).o

$(COMMAND): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)