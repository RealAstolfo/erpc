#include "rpc_node.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"

int add(int x, int y) { return x + y; }

int main() {
  std::cout << "Testing TCP..." << std::endl;
  {
    tcp_resolver resolver;
    const endpoint serv = resolver.resolve("127.0.0.1", "9999").front();
    const endpoint any;

    rpc_node<tcp_socket> tcp_based_rpc_client(any, 0);
    tcp_based_rpc_client.register_function<decltype(add), int, int>("add", add);

    tcp_based_rpc_client.subscribe(serv);
    int result = tcp_based_rpc_client.call<decltype(add), int, int>(
        &tcp_based_rpc_client.providers[0], "add", 1, 2);

    std::cout << "Result: " << result << std::endl;
    tcp_based_rpc_client.internal
        .close(); // TODO: make an rpc that announces closure.
  }

  // TODO: In order to support SSL rpc, i need the ability to generate my own
  // cert and keys. or ability to load them

  // std::cout << "Testing SSL..." << std::endl;
  // {
  //   ssl_resolver resolver;
  //   const endpoint serv = resolver.resolve("127.0.0.1", "10001").front();
  //   const endpoint any;
  //   rpc_node<ssl_socket> ssl_based_rpc_client(any, 0);
  //   ssl_based_rpc_client.register_function<decltype(add), int, int>("add",
  //   add);

  //   ssl_based_rpc_client.subscribe(serv);
  //   int result = ssl_based_rpc_client.call<decltype(add), int, int>(
  //       &ssl_based_rpc_client.providers[0], "add", 1, 2);

  //   std::cout << "Result: " << result << std::endl;
  //   ssl_based_rpc_client.internal
  //       .close(); // TODO: make an rpc that announces closure.
  // }

  // TODO: In order to support UDP rpc, i need to write an RPC header to
  // standardize the means of communication, since currently i leverage the fact
  // that TCP generates new sockets per connection, it doesnt have a uuid nor a
  // sequence number.

  // std::cout << "Testing UDP..." << std::endl;
  // {
  //   udp_resolver resolver;
  //   const endpoint serv = resolver.resolve("127.0.0.1", "10003").front();
  //   const endpoint e = resolver.resolve("127.0.0.1", "10002").front();

  //   rpc_node<udp_socket> udp_based_rpc_client(e, 0);
  //   udp_based_rpc_client.register_function<decltype(add), int, int>("add",
  //   add);

  //   udp_based_rpc_client.subscribe(serv);
  //   int result = udp_based_rpc_client.call<decltype(add), int, int>(
  //       &udp_based_rpc_client.providers[0], "add", 1, 2);

  //   std::cout << "Result: " << result << std::endl;
  // }

  return 0;
}
