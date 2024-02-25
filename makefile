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

erpc-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/erpc_test.cpp -o $@

erpc-test-client.o:
	${CXX} ${CXXFLAGS} -c builds/test/erpc_test_client.cpp -o $@

erpc-test: rpc-node.o erpc-test.o
	${CXX} ${CXXFLAGS} $^ -o $@

erpc-test-client: rpc-node.o erpc-test-client.o
	${CXX} ${CXXFLAGS} $^ -o $@

all: erpc-test erpc-test-client

clean:
	-rm -f *.o
	make -C vendors/enet clean
