#include "netvar.hpp"
#include "rpc_node.hpp"
#include "tcp.hpp"

struct network_global {
  std::string msg;
};

template <>
erpc_node<tcp_socket> netvar<network_global, tcp_socket>::netvar_transport;

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  tcp_resolver resolver;
  const endpoint serv = resolver.resolve("127.0.0.1", "7777").front();

  netvar<network_global, tcp_socket>::netvar_transport.bind(serv, 1);
  network_global message = {.msg = "Hello World"};
  netvar<network_global, tcp_socket> global_message(message);

  return 0;
}
