#include "functions.hpp"
#include "rpc_node.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"
#include <cmath>
#include <cstdio>

int main() {

  std::cout << "Testing HTTP..." << std::endl;
  {
    http_resolver resolver;
    const endpoint e = resolver.resolve("127.0.0.1", "10001").front();

    erpc_node<http_socket> http_based_rpc_server(e, 1);
    http_based_rpc_server.register_function(execute);
    http_based_rpc_server.register_function(write_stdin);
    http_based_rpc_server.register_function(read_stdout);

    http_based_rpc_server.accept();
    while (true) {
      http_based_rpc_server.respond(&http_based_rpc_server.subscribers[0]);
    }

    http_based_rpc_server.internal
        .close(); // TODO: Add rpc to announce closure.
  }

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
