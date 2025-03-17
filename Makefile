.PHONY := clean all

all:
	gcc -o chat chat.c
	#gcc -o client client.c
	#gcc -o ex ex.c
ex:
	gcc -o ex ex.c

clean:
	rm -rf app client
	rm -rf ex
	rm -rf chat
