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
#include "rpc_node.hpp"

struct prog_pipes {
  std::uintptr_t write_pipe;
  std::uintptr_t read_pipe;
};

template <typename S> void serialize(S &s, prog_pipes &pp) {
  s.value8b(pp.write_pipe);
  s.value8b(pp.read_pipe);
}

int main(int argc, char **argv) {
  arguments args;
  std::string command;
  args.add_handler("-c", [&command](const std::string_view &cmd) {
    command = std::string(cmd);
    return false;
  });
  args.process_args(argc, argv);

  const auto execute = [](const std::string cmd) -> prog_pipes {
    prog_pipes pp = {0, 0};
    std::array<int, 2> pipes_to_child;
    std::array<int, 2> pipes_from_child;
    if (pipe(pipes_to_child.data()) == -1 ||
        pipe(pipes_from_child.data()) == -1)
      std::cerr << "error creating pipes" << std::endl;

    pid_t pid;
    if ((pid = fork()) == -1)
      std::cerr << "fork failed" << std::endl;

    if (pid == 0) {
      // child
      close(pipes_to_child[1]);
      close(pipes_from_child[0]);

      if (dup2(pipes_to_child[0], STDIN_FILENO) == -1)
        std::cerr << "Error duplicating file descriptor" << std::endl;
      close(pipes_to_child[0]);

      if (dup2(pipes_from_child[1], STDOUT_FILENO) == -1)
        std::cerr << "Error duplicating file descriptor" << std::endl;
      close(pipes_from_child[1]);

      execlp("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL);
      // will never go past here.

    } else {
      // parent
      close(pipes_to_child[0]);
      close(pipes_from_child[1]);
    }

    fcntl(pipes_from_child[0], F_SETFL, O_NONBLOCK);
    // also parent.
    pp.write_pipe = pipes_to_child[1];
    pp.read_pipe = pipes_from_child[0];
    return pp;
  };

  const auto write_stdin = [](prog_pipes pipe_ptrs,
                              std::string input) -> std::string {
    int pipe = (int)pipe_ptrs.write_pipe;
    if (!pipe)
      return "Program not running";

    input += '\n';

    auto len = write(pipe, input.c_str(), input.length());
    (void)len;
    return "";
  };

  const auto read_stdout = [](prog_pipes pipe_ptrs) -> std::string {
    int pipe = (int)pipe_ptrs.read_pipe;
    std::array<char, 128> buffer;
    std::string ret;
    ssize_t bytes_read = 0;
    do {
      bytes_read = read(pipe, std::data(buffer), std::size(buffer));
      if (bytes_read > 0)
        ret.append(std::data(buffer), bytes_read);
    } while (bytes_read > 0);

    return ret;
  };

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
