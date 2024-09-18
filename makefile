CXX = zig c++

INC = -I./include -I./vendors -I./vendors/enet/include -I./vendors/enet/vendors/i2pd/libi2pd -I./vendors/exstd/include -I./vendors/exstd/vendors/bitsery/include
LIB =  -L. -L/usr/lib64 -L/usr/local/lib64

CFLAGS = -march=native -O3 -flto -g -Wall -Wextra -pedantic $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)
LDFLAGS = $(LIB) -O3

SSL = `pkgconf --cflags --libs openssl`
ZLIB = `pkgconf --cflags --libs zlib`
MD4 = `pkgconf --cflags --libs libmd`
UUID = `pkgconf --cflags --libs uuid`

# I2P = -L./vendors/i2pd -Wl,-Bstatic -li2pd -Wl,-Bdynamic -lssl -lcrypto -lz -lboost_system -lboost_program_options -lboost_filesystem

# i2p.o:
# 	${CXX} ${CXXFLAGS} -c vendors/enet/src/i2p.cpp -o $@

# vendors/enet/vendors/i2pd/libi2pd.a:
# 	make -C vendors/i2pd libi2pd.a

rpc-node.o:
	${CXX} ${CXXFLAGS} -c src/rpc_node.cpp -o $@

netvar_server.o:
	${CXX} ${CXXFLAGS} -c builds/netvar/netvar_server.cpp -o $@

netvar_client.o:
	${CXX} ${CXXFLAGS} -c builds/netvar/netvar_client.cpp -o $@

erpc-test-client.o:
	${CXX} ${CXXFLAGS} -c builds/test/erpc_test_client.cpp -o $@

erpc-test-server.o:
	${CXX} ${CXXFLAGS} -c builds/test/erpc_test_server.cpp -o $@

erpc-test-client: rpc-node.o erpc-test-client.o # i2p.o vendors/enet/vendors/i2pd/libi2pd.a
	${CXX} ${CXXFLAGS} $^ ${SSL} ${I2P} ${ZLIB} ${MD4} -o $@

erpc-test-server: rpc-node.o erpc-test-server.o # i2p.o vendors/enet/vendors/i2pd/libi2pd.a
	${CXX} ${CXXFLAGS} $^ ${SSL} ${I2P} ${ZLIB} ${MD4} -o $@

control.o:
	${CXX} ${CXXFLAGS} -c builds/c2/control.cpp -o $@

control: rpc-node.o control.o
	${CXX} ${CXXFLAGS} $^ ${SSL} ${ZLIB} ${MD4} -o $@

implant.o:
	${CXX} ${CXXFLAGS} -c builds/c2/implant.cpp -o $@

implant: rpc-node.o implant.o
	${CXX} ${CXXFLAGS} $^ ${SSL} ${ZLIB} ${MD4} -o $@

netvar_server: netvar_server.o rpc-node.o
	${CXX} ${CXXFLAGS} $^ ${SSL} ${ZLIB} ${MD4} ${UUID} -o $@

netvar_client: netvar_client.o rpc-node.o
	${CXX} ${CXXFLAGS} $^ ${SSL} ${ZLIB} ${MD4} ${UUID} -o $@


all: erpc-test-client erpc-test-server control implant netvar_server netvar_client

clean:
	-rm -f *.o control implant erpc-test-server erpc-test-client netvar_server netvar_client
	make -C vendors/enet clean
