CC := g++
SRC = ./cpp_src/*.cc
TAR = ./go_env.so

all:
	$(CC) $(SRC) -std=gnu++11 -shared -fPIC -o $(TAR)

clean:
	rm -rf $(TAR)