#include "rpc_node.hpp"
#include "tcp.hpp"

int add(int x, int y) { return x + y; }

int main() {
  tcp_resolver resolver;
  const endpoint e = resolver.resolve("127.0.0.1", "9999").front();

  rpc_node tcp_based_rpc_server(e, 1);
  tcp_based_rpc_server.register_function<decltype(add), int, int>("add", add);

  tcp_based_rpc_server.accept();
  tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);

  return 0;
}
