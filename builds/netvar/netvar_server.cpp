#include "netvar.hpp"
#include "tcp.hpp"
#include <iostream>

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
  netvar<network_global, tcp_socket>::netvar_service netvar_transport_tcp(serv,
                                                                          1);
  netvar<network_global, tcp_socket>::netvar_interface = &netvar_transport_tcp;
  netvar_transport_tcp.accept();
  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);

  std::cout
      << netvar<network_global, tcp_socket>::lookup.begin()->second->get().msg
      << std::endl;

  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);

  // network_global message = {.msg = "Hello World"};
  // netvar<network_global> global_message(message);

  std::cout
      << netvar<network_global, tcp_socket>::lookup.begin()->second->get().msg
      << std::endl;

  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);
  return 0;
}
