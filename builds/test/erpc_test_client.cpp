#include "rpc_node.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"
#include <cmath>
#include <cstdint>

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
    const endpoint serv = resolver.resolve("127.0.0.1", "9999").front();
    const endpoint any;

    erpc_node<tcp_socket> tcp_based_rpc_client(any, 0);
    tcp_based_rpc_client.register_function(add);
    tcp_based_rpc_client.register_function(sum_my_struct);
    tcp_based_rpc_client.register_function(lamb);

    tcp_based_rpc_client.subscribe(serv);
    int result = tcp_based_rpc_client.call(&tcp_based_rpc_client.providers[0],
                                           add, 1, 2);
    std::cout << "Result: " << result << std::endl;
    result = tcp_based_rpc_client.call(&tcp_based_rpc_client.providers[0], add,
                                       6, 2);
    std::cout << "Result: " << result << std::endl;

    MyStruct ms = {5.5f, 10};
    std::float_t fresult = tcp_based_rpc_client.call(
        &tcp_based_rpc_client.providers[0], sum_my_struct, std::move(ms));
    std::cout << "Result: " << fresult << std::endl;

    ms = {12.3456789, 24};
    ms = tcp_based_rpc_client.call(&tcp_based_rpc_client.providers[0], lamb,
                                   std::move(ms));
    std::cout << "MyStruct.x: " << ms.x << " MyStruct.y: " << (int)ms.y
              << std::endl;

    tcp_based_rpc_client.internal
        .close(); // TODO: make an rpc that announces closure.
  }

  // TODO: In order to support SSL rpc, i need the ability to generate my own
  // cert and keys. or ability to load them

  // hardcode sleep, since our test has the server launch, it may need some time
  sleep(3);
  std::cout << "Testing SSL..." << std::endl;
  {
    ssl_resolver resolver;
    const endpoint serv = resolver.resolve("127.0.0.1", "10001").front();
    const endpoint any;
    erpc_node<ssl_socket> ssl_based_rpc_client(any, 0);
    ssl_based_rpc_client.register_function(add);

    ssl_based_rpc_client.subscribe(serv);
    int result = ssl_based_rpc_client.call(&ssl_based_rpc_client.providers[0],
                                           add, 1, 2);

    std::cout << "Result: " << result << std::endl;
    ssl_based_rpc_client.internal
        .close(); // TODO: make an rpc that announces closure.
  }

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
