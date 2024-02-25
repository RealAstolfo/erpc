#include "rpc_node.hpp"
#include "tcp.hpp"

int add(int x, int y) { return x + y; }

int main() {
  tcp_resolver resolver;
  const endpoint e = resolver.resolve("127.0.0.1", "9999").front();

  rpc_node<tcp_socket> tcp_based_rpc_client(e, 0);
  tcp_based_rpc_client.register_function<decltype(add), int, int>("add", add);

  tcp_based_rpc_client.subscribe(e);
  int result = tcp_based_rpc_client.call<decltype(add), int, int>(
      &tcp_based_rpc_client.subscribers[0], "add", 1, 2);

  std::cout << "Result: " << result << std::endl;
  return 0;
}
