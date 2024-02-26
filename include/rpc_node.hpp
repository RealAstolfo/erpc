#ifndef ERPC_RPC_NODE_HPP
#define ERPC_RPC_NODE_HPP

#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
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
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<std::byte>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<std::byte>>;
    using result_t = std::invoke_result_t<Func, Args...>;
    using func_args = std::tuple<Args...>;

    lookup.emplace(std::move(func_name), [function](
                                             T *from,
                                             std::vector<std::byte> &buf) {
      func_args arguments;
      auto state = bitsery::quickDeserialization<input_adapter>(
          {std::begin(buf), std::end(buf)}, arguments);
      assert(state.first == bitsery::ReaderError::NoError && state.second);

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments);
      } else {
        std::variant<size_t, std::array<std::byte, sizeof(size_t)>> byte_len;

        auto result = std::make_tuple(std::apply(function, arguments));
        std::get<size_t>(byte_len) =
            bitsery::quickSerialization<output_adapter>(buf, result);
        from->send(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
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
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<std::byte>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<std::byte>>;
    using return_t = std::invoke_result_t<Func, Args...>;
    using func_args = std::tuple<Args...>;

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    std::vector<std::byte> buf;

    const auto function = *iter;
    const func_args fargs = std::make_tuple(args...);

    std::variant<size_t, std::array<std::byte, sizeof(size_t)>> byte_len;
    std::get<size_t>(byte_len) =
        bitsery::quickSerialization<output_adapter>(buf, func_name);

    target->send(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
    target->send(buf);
    buf.clear();

    std::get<size_t>(byte_len) =
        bitsery::quickSerialization<output_adapter>(buf, fargs);
    target->send(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
    target->send(buf);
    buf.clear();

    if constexpr (std::is_void_v<return_t>)
      return;

    target->receive_some(
        std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
    buf.resize(std::get<size_t>(byte_len));
    target->receive_some(buf);

    std::tuple<return_t> return_val;
    auto state = bitsery::quickDeserialization<input_adapter>(
        {std::begin(buf), std::size(buf)}, return_val);
    assert(state.first == bitsery::ReaderError::NoError && state.second);

    return std::get<0>(return_val);
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, send. This function will also block until there is
    something to respond to.
   */
  void respond(T *const to) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<std::byte>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<std::byte>>;
    std::variant<size_t, std::array<std::byte, sizeof(size_t)>> byte_len;

    std::vector<std::byte> buf;

    to->receive_some(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));

    buf.resize(std::get<size_t>(byte_len));
    to->receive_some(buf);

    std::string func_name;
    auto state = bitsery::quickDeserialization<input_adapter>(
        {std::begin(buf), std::size(buf)}, func_name);
    assert(state.first == bitsery::ReaderError::NoError && state.second);

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    to->receive_some(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
    buf.resize(std::get<size_t>(byte_len));
    to->receive_some(buf);

    auto return_val = iter(to, buf);
    buf.clear();

    std::get<size_t>(byte_len) = bitsery::quickSerialization<output_adapter>(
        buf, std::make_tuple(return_val));
    to->send(std::get<std::array<std::byte, sizeof(size_t)>>(byte_len));
    to->send(buf);
    return;
  }

  std::unordered_map<std::string, std::function<void(std::vector<std::byte> &)>>
      lookup;
  std::vector<T> subscribers;
  std::vector<T> providers;

  T internal;
};

#endif
