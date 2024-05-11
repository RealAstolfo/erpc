#include "endpoint.hpp"
#include "netvar.hpp"
#include "rpc_node.hpp"
#include "tcp.hpp"
#include <unordered_map>

struct network_global {
  std::string msg;
};

template <typename S> void serialize(S &s, network_global &ng) {
  s.template text<sizeof(std::string::value_type)>(ng.msg, 1024);
}

template <>
netvar<network_global, tcp_socket>::netvar_service
    *netvar<network_global, tcp_socket>::netvar_interface = nullptr;

template <>
std::unordered_map<std::string, netvar<network_global, tcp_socket> *>
    netvar<network_global, tcp_socket>::lookup = {};

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  tcp_resolver resolver;
  const endpoint serv = resolver.resolve("127.0.0.1", "7777").front();
  const endpoint any;

  netvar<network_global, tcp_socket>::netvar_service netvar_transport_tcp(any,
                                                                          0);
  netvar<network_global, tcp_socket>::netvar_interface = &netvar_transport_tcp;
  netvar_transport_tcp.subscribe(serv);
  network_global message = {.msg = "Hello World"};
  netvar<network_global, tcp_socket> global_message(message);
  message.msg = "Modified!";
  global_message.set(message);
  return 0;
}
