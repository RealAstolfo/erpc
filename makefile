CXX ?= g++
AR ?= gcc-ar

NAME = erpc
LIBA = lib$(NAME).a

# Sibling e* libraries (exstd, enet) and third-party headers are provided by
# Guix on the compiler search path (CPATH / LIBRARY_PATH).  Only this repo's
# own headers are referenced explicitly.
INC = -I./include

CFLAGS = -march=native -O3 -flto -g -Wall -Wextra -pedantic $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)

# mostly-static: static libstdc++/libgcc and static archives for the e* libs;
# glibc stays dynamic.  No global -static.
STATICFLAGS = -static-libstdc++ -static-libgcc
# -L. so -lerpc resolves the locally-built liberpc.a (sibling archives come from
# the Guix inputs on LIBRARY_PATH).
LDFLAGS = -L. -O3 $(STATICFLAGS)

# e* siblings install lib<name>.a; link them statically.  exstd is header-only
# (no archive), so it is NOT listed here -- its symbols are inline in the .o.
ELIBS = -Wl,-Bstatic -lerpc -lenet -Wl,-Bdynamic

# Third-party libs: whole-chain-static.  Headers come from the Guix inputs via
# CPATH, so only the link flags are needed; each lib is pulled from its static
# archive (openssl/zlib "static" outputs, libmd-static, util-linux-static) and
# wrapped in -Bstatic/-Bdynamic so glibc stays dynamic.
SSL_LIBS  = -Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic
ZLIB_LIBS = -Wl,-Bstatic -lz -Wl,-Bdynamic
MD4_LIBS  = -Wl,-Bstatic -lmd -Wl,-Bdynamic
UUID_LIBS = -Wl,-Bstatic -luuid -Wl,-Bdynamic

# --- library ---------------------------------------------------------------

# Library objects that go into lib$(NAME).a.  netvar is header-only, so the
# sole compiled library translation unit is rpc_node.
LIBOBJS = rpc-node.o

rpc-node.o: src/rpc_node.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LIBA): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

lib: $(LIBA)

# --- example / build binaries ---------------------------------------------

netvar_server.o: builds/netvar/netvar_server.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

netvar_client.o: builds/netvar/netvar_client.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

erpc-test-client.o: builds/test/erpc_test_client.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

erpc-test-server.o: builds/test/erpc_test_server.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

control.o: builds/c2/control.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

implant.o: builds/c2/implant.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

erpc-test-client: erpc-test-client.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) -o $@

erpc-test-server: erpc-test-server.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) -o $@

control: control.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) -o $@

implant: implant.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) -o $@

netvar_server: netvar_server.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) $(UUID_LIBS) -o $@

netvar_client: netvar_client.o $(LIBA)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(ELIBS) $(SSL_LIBS) $(ZLIB_LIBS) $(MD4_LIBS) $(UUID_LIBS) -o $@

all: erpc-test-client erpc-test-server control implant netvar_server netvar_client

# --- install ---------------------------------------------------------------

PREFIX ?= /usr/local
DESTDIR ?=

install: $(LIBA)
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include
	cp $(LIBA) $(DESTDIR)$(PREFIX)/lib/
	cp -r include/. $(DESTDIR)$(PREFIX)/include/

# --- tooling ---------------------------------------------------------------

clangd:
	bear -- make all

clean:
	-rm -f *.o *.a control implant erpc-test-server erpc-test-client netvar_server netvar_client


# Position-independent code: required so each repo's static archive can be
# bundled into the eengine umbrella shared library (libeengine.so).
CFLAGS   += -fPIC
CXXFLAGS += -fPIC
.PHONY: all lib install clangd clean
