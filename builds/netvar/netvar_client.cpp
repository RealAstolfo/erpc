#include "endpoint.hpp"
#include "netvar.hpp"
#include "rpc_node.hpp"
#include "singleton.hpp"
#include "tcp.hpp"
#include <unordered_map>

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
  const endpoint any;

  netvar_service<tcp_socket, network_global, network_number> nvs(any, 0);
  nvs.subscribe(serv);
  network_global message = {.msg = "Hello World"};
  netvar<tcp_socket, network_global> global_message(message);
  message.msg = "Modified!";
  global_message = message;

  network_number number = {.x = 10};
  netvar<tcp_socket, network_number> nm(number);
  number.x = 69;
  nm = number;

  return 0;
}
