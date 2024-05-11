#include "netvar.hpp"
#include "singleton.hpp"
#include "tcp.hpp"
#include <iostream>

struct network_global {
  std::string msg;
};

struct network_number {
  int x;
};

template <typename S> void serialize(S &s, network_global &ng) {
  s.template text<sizeof(std::string::value_type)>(ng.msg, 1024);
}

template <typename S> void serialize(S &s, network_number &nm) {
  s.value4b(nm.x);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  tcp_resolver resolver;
  const endpoint serv = resolver.resolve("127.0.0.1", "7777").front();
  netvar_service<tcp_socket, network_global, network_number> nvs(serv, 1);

  nvs.accept();
  nvs.respond(&nvs.subscribers[0]);

  auto &msg_lookup = singleton<std::unordered_map<
      std::string, netvar<tcp_socket, network_global> *>>::instance();
  auto &num_lookup = singleton<std::unordered_map<
      std::string, netvar<tcp_socket, network_number> *>>::instance();

  {
    const network_global &nv = *(msg_lookup.begin()->second);
    std::cout << nv.msg << std::endl;
  }

  nvs.respond(&nvs.subscribers[0]);

  // network_global message = {.msg = "Hello World"};
  // netvar<network_global> global_message(message);

  {
    const network_global &nv = *(msg_lookup.begin()->second);
    std::cout << nv.msg << std::endl;
  }

  nvs.respond(&nvs.subscribers[0]);
  {
    const network_number &nm = *(num_lookup.begin()->second);
    std::cout << "My Number: " << nm.x << std::endl;
  }

  nvs.respond(&nvs.subscribers[0]);
  {
    const network_number &nm = *(num_lookup.begin()->second);
    std::cout << "My Number: " << nm.x << std::endl;
  }

  nvs.respond(&nvs.subscribers[0]);
  nvs.respond(&nvs.subscribers[0]);
  return 0;
}
