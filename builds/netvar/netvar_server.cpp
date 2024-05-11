#include "netvar.hpp"
#include "singleton.hpp"
#include "tcp.hpp"
#include <iostream>

struct network_global {
  std::string msg;
};

template <typename S> void serialize(S &s, network_global &ng) {
  s.template text<sizeof(std::string::value_type)>(ng.msg, 1024);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  tcp_resolver resolver;
  const endpoint serv = resolver.resolve("127.0.0.1", "7777").front();
  netvar<network_global, tcp_socket>::netvar_service netvar_transport_tcp(serv,
                                                                          1);

  netvar_transport_tcp.accept();
  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);

  auto &lookup = singleton<std::unordered_map<
      std::string, netvar<network_global, tcp_socket> *>>::instance();

  {
    const network_global &nv = *(lookup.begin()->second);
    std::cout << nv.msg << std::endl;
  }

  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);

  // network_global message = {.msg = "Hello World"};
  // netvar<network_global> global_message(message);

  {
    const network_global &nv = *(lookup.begin()->second);
    std::cout << nv.msg << std::endl;
  }

  netvar_transport_tcp.respond(&netvar_transport_tcp.subscribers[0]);
  return 0;
}
