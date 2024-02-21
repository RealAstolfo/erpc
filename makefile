CC = gcc
CXX = g++
LD = ld
AR = ar
AS = as

INC = -I./include -I./vendors -I./vendors/enet/include -I./vendors/exstd/vendors/bitsery/include
LIB =  -L. -L/usr/lib64 -L/usr/local/lib64

CFLAGS = -march=native -O3 -g -Wall -Wextra -pedantic $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)
LDFLAGS = $(LIB) -O3

rpc-node.o:
	${CXX} ${CXXFLAGS} -c src/rpc_node.cpp -o $@


all: 

clean:
	-rm -f *.o
	make -C vendors/enet clean
