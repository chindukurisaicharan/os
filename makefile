CC = gcc

CFLAGS = -Wall -Wextra -Iinclude

LDFLAGS = -lreadline

SRC = src/main.c \
      src/token.c \
      src/lexer.c \
      src/parser.c \
      src/expand.c \
      src/history.c \
      src/builtin.c \
      src/executor.c

OBJ = $(SRC:.c=.o)

TARGET = shellforge


$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(OBJ) $(TARGET)
