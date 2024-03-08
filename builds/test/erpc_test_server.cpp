#include "rpc_node.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"
#include <cmath>

struct MyStruct {
  std::float_t x;
  uint8_t y;
};

template <typename S> void serialize(S &s, MyStruct &ms) {
  s.value4b(ms.x);
  s.value1b(ms.y);
}

int add(int x, int y) { return x + y; }

std::float_t sum_my_struct(MyStruct ms) { return ms.x + ms.y; }

int main() {
  const auto lamb = [](MyStruct ms) {
    ms.x *= 2;
    ms.y /= 2;
    return ms;
  };

  std::cout << "Testing TCP..." << std::endl;
  {
    tcp_resolver resolver;
    const endpoint e = resolver.resolve("127.0.0.1", "9999").front();

    erpc_node<tcp_socket> tcp_based_rpc_server(e, 1);
    tcp_based_rpc_server.register_function(add);
    tcp_based_rpc_server.register_function(sum_my_struct);
    tcp_based_rpc_server.register_function(lamb);

    tcp_based_rpc_server.accept();
    tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);
    tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);
    tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);
    tcp_based_rpc_server.respond(&tcp_based_rpc_server.subscribers[0]);
    tcp_based_rpc_server.internal.close(); // TODO: Add rpc to announce closure.
  }

  // TODO: In order to support SSL rpc, i need the ability to generate my own
  // cert and keys. or ability to load them

  // std::cout << "Testing SSL..." << std::endl;
  // {
  //   ssl_resolver resolver;
  //   const endpoint e = resolver.resolve("127.0.0.1", "10000").front();

  //   rpc_node<ssl_socket> ssl_based_rpc_server(e, 1);
  //   ssl_based_rpc_server.register_function<decltype(add), int, int>("add",
  //   add);

  //   ssl_based_rpc_server.accept();
  //   ssl_based_rpc_server.respond(&ssl_based_rpc_server.subscribers[0]);
  //   ssl_based_rpc_server.internal.close(); // TODO: Add rpc to announce
  //   closure.
  // }

  // TODO: In order to support UDP rpc, i need to write an RPC header to
  // standardize the means of communication, since currently i leverage the fact
  // that TCP generates new sockets per connection, it doesnt have a uuid nor a
  // sequence number.

  // std::cout << "Testing UDP..." << std::endl;
  // {
  //   udp_resolver resolver;
  //   const endpoint e = resolver.resolve("127.0.0.1", "10003").front();

  //   rpc_node<udp_socket> udp_based_rpc_server(e, 1);
  //   udp_based_rpc_server.register_function<decltype(add), int, int>("add",
  //   add);

  //   udp_based_rpc_server.accept();
  //   udp_based_rpc_server.respond(&udp_based_rpc_server.subscribers[0]);
  // }

  return 0;
}
