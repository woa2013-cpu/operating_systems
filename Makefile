CC = gcc
CFLAGS = -Wall -Wextra

TARGET = myshell

OBJS = myshell.o parser.o execute.o redirection.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

myshell.o: myshell.c
	$(CC) $(CFLAGS) -c myshell.c

parser.o: parser.c
	$(CC) $(CFLAGS) -c parser.c

execute.o: execute.c redirection.h
	$(CC) $(CFLAGS) -c execute.c

redirection.o: redirection.c redirection.h
	$(CC) $(CFLAGS) -c redirection.c

clean:
	rm -f $(OBJS) $(TARGET)
