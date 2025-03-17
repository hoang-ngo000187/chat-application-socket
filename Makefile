.PHONY := clean all

all:
	gcc -o chat chat.c

clean:
	rm -rf chat
