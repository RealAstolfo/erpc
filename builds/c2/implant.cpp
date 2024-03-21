#include "rpc_node.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"
#include <cmath>
#include <cstdio>

struct prog_pipes {
  std::uintptr_t write_pipe;
  std::uintptr_t read_pipe;
};

template <typename S> void serialize(S &s, prog_pipes &pp) {
  s.value8b(pp.write_pipe);
  s.value8b(pp.read_pipe);
}

int main() {
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
