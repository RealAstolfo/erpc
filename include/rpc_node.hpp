#ifndef ERPC_RPC_NODE_HPP
#define ERPC_RPC_NODE_HPP

#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/std_tuple.h>
#include <bitsery/traits/array.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include "tcp.hpp"

/*
  One must pick a socket type for "T", a later example will show a TCP example.
 */
template <typename T> struct rpc_node {

  /*
    By default, a node should not serve calls.
    Parameter "ep" in the context of binding is a local address.
   */
  rpc_node(const endpoint ep, const int max_incoming_connections = 0) {
    internal.bind(ep);

    if (max_incoming_connections)
      internal.listen(max_incoming_connections);
  }

  ~rpc_node() { internal.close(); }

  template <typename Func, typename... Args>
  void register_function(std::string func_name, Func function) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<uint8_t>>;
    using result_t = std::invoke_result_t<Func, Args...>;
    using func_args = std::tuple<Args...>;

    lookup.emplace(
        std::move(func_name), [function](T *from, std::vector<uint8_t> &buf) {
          func_args arguments;
          auto state = bitsery::quickDeserialization<input_adapter>(
              {std::begin(buf), std::end(buf)}, arguments);

          assert(state.first == bitsery::ReaderError::NoError && state.second);

          if constexpr (std::is_void_v<result_t>) {
            std::apply(function, arguments);
          } else {
            auto result = std::apply(function, arguments);
            bitsery::quickSerialization<output_adapter>(buf, result);
            from->send(buf);
          }
        });
  }

  /*
    Subscribe to a node, this allows you to execute functions on the device you
    subscribed to.

    Will return if it was successful or not.
   */
  bool subscribe(const endpoint e) {
    T socket;
    socket.connect(e);
    providers.emplace_back(socket);
    return true;
  }

  /*
    Accept a node trying to subscribe to your services.
    This blocks until a node tries to subscribe.
   */
  void accept() { subscribers.emplace_back(internal.accept()); }

  /*
    Invoke a registered function "std::string func_name" on the target node "T
    *target" using the parameters for the function "Args &&...args"

    Internally, it will serialize the arguments and call on the target remote.
   */
  template <typename Func, typename... Args>
  std::invoke_result_t<Func, Args...>
  call(T *const target, std::string func_name, Args &&...args) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<uint8_t>>;
    using return_t = std::invoke_result_t<Func, Args...>;
    using func_args = std::tuple<Args...>;

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    std::vector<uint8_t> buf;

    const auto function = *iter;
    const func_args fargs = std::make_tuple(std::forward<Args>(args)...);

    auto written_size = bitsery::quickSerialization<output_adapter>(buf, fargs);

    // we must send the function name by sending the length of it first.
    std::variant<uint32_t, std::array<char, sizeof(uint32_t)>> bytes;
    std::get<uint32_t>(bytes) = func_name.length();
    target->send(std::get<std::array<char, sizeof(uint32_t)>>(bytes));
    target->send(func_name);

    // after, lets send the arguments
    target->send(buf);

    if constexpr (std::is_void_v<return_t>)
      return;

    // ensures lower layer attempts to read exactly enough.
    std::variant<return_t, std::array<char, sizeof(return_t)>> return_val;
    const size_t recv_len = target->receive(
        std::get<std::array<char, sizeof(return_t)>>(return_val));
    const auto state = bitsery::quickDeserialization<input_adapter>(
        {std::begin(buf), recv_len}, std::get<return_t>(return_val));
    assert(state.first == bitsery::ReaderError::NoError && state.second);
    return std::get<return_t>(return_val);
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, send. This function will also block until there is
    something to respond to.
   */
  void respond(T *const to) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
    std::variant<uint32_t, std::array<char, sizeof(uint32_t)>> str_len;
    to->receive_some(std::get<std::array<char, sizeof(uint32_t)>>(str_len));

    std::string func_name;
    func_name.resize(std::get<uint32_t>(str_len));
    to->receive_some(func_name);
    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    std::vector<uint8_t> buf;
    ssize_t bytes = 0;
    do {
      bytes = to->receive(buf);
    } while (bytes > 0);

    auto return_val = iter(to, buf);
    buf.clear();
    auto written_size =
        bitsery::quickSerialization<output_adapter>(buf, return_val);
    to->send(buf);
    return;
  }

  std::unordered_map<std::string, std::function<void(std::vector<uint8_t> &)>>
      lookup;
  std::vector<T> subscribers;
  std::vector<T> providers;

  T internal;
};

#endif
