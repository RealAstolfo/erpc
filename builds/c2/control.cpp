#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <future>
#include <thread>
#include <unistd.h>

#include "args.hpp"
#include "functions.hpp"
#include "rpc_node.hpp"

int main(int argc, char **argv) {
  arguments args;
  std::string command;
  args.add_handler("-c", [&command](const std::string_view &cmd) {
    command = std::string(cmd);
    return false;
  });
  args.process_args(argc, argv);

  std::cout << "Testing HTTP..." << std::endl;
  {
    http_resolver resolver;
    const endpoint serv = resolver.resolve("127.0.0.1", "10001").front();
    const endpoint any;
    erpc_node<http_socket> http_based_rpc_client(any, 0);
    http_based_rpc_client.register_function(execute);
    http_based_rpc_client.register_function(write_stdin);
    http_based_rpc_client.register_function(read_stdout);

    http_based_rpc_client.subscribe(serv);
    prog_pipes program_ptr = http_based_rpc_client.call(
        &http_based_rpc_client.providers[0], execute, command);

    while (true) {
      if (program_ptr.read_pipe != 0 && program_ptr.write_pipe != 0) {

        std::future_status status;
        std::future<std::string> input_future =
            std::async(std::launch::async, []() {
              std::string user_input;
              std::getline(std::cin, user_input);
              return user_input;
            });

        do {
          switch (status =
                      input_future.wait_for(std::chrono::milliseconds(0))) {
          case std::future_status::deferred:
            break;
          case std::future_status::timeout:
            std::cout << http_based_rpc_client.call(
                &http_based_rpc_client.providers[0], read_stdout, program_ptr);
            break;
          case std::future_status::ready:
            std::string user_input = input_future.get();

            if (!user_input.empty()) {
              std::cerr << http_based_rpc_client.call(
                  &http_based_rpc_client.providers[0], write_stdin, program_ptr,
                  std::move(user_input));
            }
            break;
          }
        } while (status != std::future_status::ready);
      }
    }

    http_based_rpc_client.internal
        .close(); // TODO: make an rpc that announces closure.
  }

  // hardcode sleep, since our test has the server launch, it may need some time
  // sleep(3);
  // std::cout << "Testing SSL..." << std::endl;
  // {
  //   i2p_resolver resolver;
  //   const endpoint serv = resolver.resolve("127.0.0.1", "10001").front();
  //   const endpoint any;
  //   erpc_node<i2p_socket> i2p_based_rpc_client(any, 0);
  //   i2p_based_rpc_client.register_function(add);

  //   i2p_based_rpc_client.subscribe(serv);
  //   int result =
  //   i2p_based_rpc_client.call(&i2p_based_rpc_client.providers[0],
  //                                          add, 1, 2);

  //   std::cout << "Result: " << result << std::endl;
  //   i2p_based_rpc_client.internal
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
