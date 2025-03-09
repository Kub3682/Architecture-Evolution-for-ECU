CC = gcc
RM = rm -rf

LIB = simsched
LIB_DIR = ../
INC_DIR = ../inc

SRCS = $(wildcard *.c)
OUTPUTS=$(SRCS:.c=)

CFLAGS = -pthread -lrt -L$(LIB_DIR) -l$(LIB) -Wl,-rpath,$(LIB_DIR) -I$(INC_DIR) -g

all: $(OUTPUTS)

$(OUTPUTS): % : %.c
	$(CC) $< -o $@ $(CFLAGS)

clean:
	rm -f $(OUTPUTS)

.PHONY: all clean
