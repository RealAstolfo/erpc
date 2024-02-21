#ifndef ERPC_RPC_NODE_HPP
#define ERPC_RPC_NODE_HPP

#include <algorithm>
#include <any>
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

#include "bitsery/deserializer.h"
#include "bitsery/serializer.h"
#include "tcp.hpp"

template <typename Func> class function_traits;
template <typename Result, typename... Args>
class function_traits<Result(Args...)> {
public:
  using arguments = std::tuple<Args...>;
};

/*
  One must pick a socket type for "T", a later example will show a TCP example.
 */
template <typename T> struct rpc_node : T {

  /*
    By default, a node should not serve calls.
    Parameter "ep" in the context of binding is a local address.
   */
  rpc_node(const endpoint ep, const int max_incoming_connections = 0) {
    T::bind(ep);

    if (max_incoming_connections)
      T::listen(max_incoming_connections);
  }

  ~rpc_node() { T::close(); }

  template <typename Func>
  void register_function(std::string func_name, Func function) {
    lookup.emplace(std::move(func_name), [function](T *from,
                                                    std::vector<uint8_t> &buf) {
      using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
      using input_adapter = bitsery::InputBufferAdapter<std::vector<uint8_t>>;
      using func_args = function_traits<Func>::arguments;

      func_args args;
      auto state = bitsery::quickDeserialization<input_adapter>(
          {std::begin(buf), sizeof(args)}, args);

      assert(state.first == bitsery::ReaderError::NoError && state.second);

      if constexpr (std::is_void_v<decltype(function())>) {
        std::apply(function, args);
      } else {
        auto result = std::apply(function, args);
        auto written_size =
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
  bool subscribe(const endpoint e) { return T::connect(e); }

  /*
    Accept a node trying to subscribe to your services.
    This blocks until a node tries to subscribe.
   */
  void accept() { subscribers.emplace_back(T::accept()); }

  /*
    Invoke a registered function "std::string func_name" on the target node "T
    *target" using the parameters for the function "Args &&...args"

    Internally, it will serialize the arguments and call on the target remote.
   */
  template <typename Func, typename... Args>
  std::invoke_result_t<Func, Args...>
  call(T const *target, std::string func_name, Args &&...args) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<uint8_t>>;
    using func_args = function_traits<Func>::arguments;
    using return_t = std::invoke_result_t<Func, Args...>;

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    std::vector<uint8_t> buf;

    const auto function = *iter;
    const func_args fargs = std::make_tuple(std::forward<Args>(args)...);
    const auto written_size =
        bitsery::quickSerialization<output_adapter>(buf, fargs);

    // we must send the function name
    const size_t str_len = target->send(func_name);

    // after, lets send the arguments
    const size_t len = target->send(buf);

    if constexpr (std::is_void_v<return_t>) {
      // ensures lower layer attempts to read exactly enough.
      buf.resize(sizeof(return_t));
      const size_t recv_len = target->receive(buf);
      return_t ret_val;
      const auto state = bitsery::quickDeserialization<input_adapter>(
          {std::begin(buf), recv_len}, ret_val);
      assert(state.first == bitsery::ReaderError::NoError && state.second);
      return ret_val;
    } else
      return;
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, send. This function will also block until there is
    something to respond to.
   */
  void respond(T const *to) {
    using output_adapter = bitsery::OutputBufferAdapter<std::vector<uint8_t>>;
    using input_adapter = bitsery::InputBufferAdapter<std::vector<uint8_t>>;
    std::vector<uint8_t> buf;
    ssize_t len = to->receive(buf);

    std::string func_name;
    auto state = bitsery::quickDeserialization<input_adapter>(
        {std::begin(buf), len}, func_name);
  }

  std::unordered_map<std::string, std::function<void(std::vector<uint8_t> &)>>
      lookup;
  std::vector<T> subscribers;
};

#endif
