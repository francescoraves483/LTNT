EXECNAME=LTNT_test_manager

CC_EMBEDDED=x86_64-openwrt-linux-musl-gcc

SRC_DIR=src
OBJ_DIR=obj

SRC_RAWSOCK_LIB_DIR=Rawsock_lib/Rawsock_lib
OBJ_RAWSOCK_LIB_DIR=Rawsock_lib/Rawsock_lib

SRC_INIH_DIR=libs/inih
OBJ_INIH_DIR=obj/inih

SRC=$(wildcard $(SRC_DIR)/*.c)
SRC_INIH_LIB=$(wildcard $(SRC_INIH_DIR)/*.c)

OBJ=$(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
OBJ_INIH_LIB=$(SRC_INIH_LIB:$(SRC_INIH_DIR)/%.c=$(OBJ_INIH_DIR)/%.o)

OBJ_CC=$(OBJ)
OBJ_CC+=$(OBJ_INIH_LIB)

CFLAGS += -Wall -O2 -Iinclude -Ilibs/inih
# LDLIBS += -lpthread -lm

.PHONY: all clean

all: compilePC

compilePC: CC = gcc
compileAPU: CC = $(CC_EMBEDDED)
	
compilePCdebug: CFLAGS += -g
compilePCdebug: compilePC

compileAPUdebug: CFLAGS += -g
compileAPUdebug: compileAPU

compilePC compileAPU compilePCdebug compileAPUdebug: $(EXECNAME)

# Standard targets
$(EXECNAME): $(OBJ_CC)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@ mkdir -p $(OBJ_DIR)
	@ mkdir -p $(OBJ_INIH_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_INIH_DIR)/%.o: $(SRC_INIH_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ_DIR)/*.o $(OBJ_FULL_DIR)/*.o $(OBJ_INIH_LIB)/*.o
	-rm -rf $(OBJ_DIR)
	-rm -rf $(OBJ_INIH_DIR)

fullclean: clean
	$(RM) $(EXECNAME)