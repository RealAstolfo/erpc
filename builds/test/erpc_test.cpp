#include "endpoint.hpp"
#include "rpc_node.hpp"
#include "tcp.hpp"

int add(int x, int y) { return x + y; }

int main(int argc, char **argv) {
  tcp_resolver resolver;
  const endpoint e = resolver.resolve("127.0.0.1", "9999").front();

  rpc_node<tcp_socket> tcp_based_rpc_server(e, 1);
  tcp_based_rpc_server.register_function("add", add);

  tcp_based_rpc_server.accept();
  tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);
}