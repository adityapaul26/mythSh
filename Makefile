CC = gcc
CFLAGS = -Wall -Iinclude

SRC = src/main.c
OBJ = $(SRC:.c=.o)
TARGET = mysh

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)
