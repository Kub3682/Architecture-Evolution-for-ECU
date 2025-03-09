CC = gcc
RM = rm -rf

SRC_DIR = ./src
INC_DIR = ./inc
OBJ_DIR = ./obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(SRCS)))
LIB = libsimsched.so

CFLAGS = -fPIC -Wall -I$(INC_DIR) -pthread -lrt -g
LDFLAGS=-shared

ifeq ($(debug), 1)
	CFLAGS += -g -DDEBUG
endif

ifeq ($(test), 1)
	CFLAGS += -DTEST
endif

all: prepare $(LIB)

$(LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

prepare:
	@if [ ! -d "$(OBJ_DIR)" ]; then \
		echo "Creating $(OBJ_DIR) directory..."; \
		mkdir $(OBJ_DIR); \
	fi

clean:
	$(RM) $(OBJS) $(LIB)

.PHONY: all clean
