CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

SRC = src/mythSh.c src/todo.c
OBJ = $(SRC:.c=.o)
TARGET = mythsh

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean
