CC = gcc

PKG_CONFIG = pkg-config

SRC_DIR = src
HDR_DIR = headers
BIN_DIR = bin

CFLAGS = -Wall -O3 -march=native -flto -I$(HDR_DIR)
LIBS = -lc -lm

TARGET = $(BIN_DIR)/ULT
SRCS = $(wildcard $(SRC_DIR)/*.c)

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

clean:
	rm -rf $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

rebuild: clean all
