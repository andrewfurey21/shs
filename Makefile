all:
	mkdir -p build
	gcc server.c -g -o ./build/server
