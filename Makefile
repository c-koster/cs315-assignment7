CFLAGS=-g -Wall -pedantic

.PHONY: all
all: chat-server chat-client

chat-server: chat-server.c
	gcc $(CFLAGS) -pthread -o $@ $^

chat-client: chat-client.c
	gcc $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f chat-server chat-client
