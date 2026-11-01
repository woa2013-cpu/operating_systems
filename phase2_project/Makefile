CC = gcc
CFLAGS = -Wall -Wextra

# Phase 1 shell engine, shared by myshell and the server
SHELL_OBJS = pipeline.o parser.o execute.o redirection.o

# Phase 1: local shell
MYSHELL_OBJS = myshell.o $(SHELL_OBJS)

# Phase 2: server runs commands with the Phase 1 engine; client only talks over the socket
SERVER_OBJS = server.o capture.o protocol.o $(SHELL_OBJS)
CLIENT_OBJS = client.o protocol.o

all: myshell server client

myshell: $(MYSHELL_OBJS)
	$(CC) $(CFLAGS) $(MYSHELL_OBJS) -o myshell

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o server

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $(CLIENT_OBJS) -o client

# Phase 1 objects
myshell.o: myshell.c pipeline.h
	$(CC) $(CFLAGS) -c myshell.c

pipeline.o: pipeline.c pipeline.h parser.h execute.h redirection.h
	$(CC) $(CFLAGS) -c pipeline.c

parser.o: parser.c parser.h redirection.h
	$(CC) $(CFLAGS) -c parser.c

execute.o: execute.c execute.h parser.h redirection.h
	$(CC) $(CFLAGS) -c execute.c

redirection.o: redirection.c redirection.h
	$(CC) $(CFLAGS) -c redirection.c

# Phase 2 objects
protocol.o: protocol.c protocol.h
	$(CC) $(CFLAGS) -c protocol.c

client.o: client.c protocol.h
	$(CC) $(CFLAGS) -c client.c

server.o: server.c protocol.h capture.h
	$(CC) $(CFLAGS) -c server.c

capture.o: capture.c capture.h pipeline.h protocol.h
	$(CC) $(CFLAGS) -c capture.c

clean:
	rm -f *.o myshell server client

.PHONY: all clean
