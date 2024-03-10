#ifndef ERPC_RPC_NODE_HPP
#define ERPC_RPC_NODE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <uuid/uuid.h>
#include <vector>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/std_tuple.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include "bitsery/deserializer.h"
#include "bitsery/serializer.h"

#include "endpoint.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"

template <typename Sig> struct return_type;

template <typename Ret, typename... Args> struct return_type<Ret(Args...)> {
  using type = Ret;
};

template <typename Ret, typename Obj, typename... Args>
struct return_type<Ret (Obj::*)(Args...)> {
  using type = Ret;
};

template <typename Ret, typename Obj, typename... Args>
struct return_type<Ret (Obj::*)(Args...) const> {
  using type = Ret;
};

template <typename Fun>
concept is_fun = std::is_function_v<Fun>;

template <typename Fun>
concept is_mem_fun = std::is_member_function_pointer_v<std::decay_t<Fun>>;

template <typename Fun>
concept is_functor = std::is_class_v<std::decay_t<Fun>> &&
                     requires(Fun &&t) { &std::decay_t<Fun>::operator(); };

template <is_functor T>
using return_type_t =
    typename return_type<decltype(&std::decay_t<T>::operator())>::type;

template <typename Sig> struct signature;
template <typename Ret, typename... Args> struct signature<Ret(Args...)> {
  using type = std::tuple<Args...>;
};

template <typename Ret, typename Obj, typename... Args>
struct signature<Ret (Obj::*)(Args...)> {
  using type = std::tuple<Args...>;
};
template <typename Ret, typename Obj, typename... Args>
struct signature<Ret (Obj::*)(Args...) const> {
  using type = std::tuple<Args...>;
};

template <is_functor T>
auto arguments(T &&t)
    -> signature<decltype(&std::decay_t<T>::operator())>::type;

template <is_functor T>
auto arguments(const T &t)
    -> signature<decltype(&std::decay_t<T>::operator())>::type;

// template<is_fun T>
// auto arguments(T&& t)->signature<T>::type;

template <is_fun T> auto arguments(const T &t) -> signature<T>::type;

template <is_mem_fun T>
auto arguments(T &&t) -> signature<std::decay_t<T>>::type;

template <is_mem_fun T>
auto arguments(const T &t) -> signature<std::decay_t<T>>::type;

template <typename obj_or_value>
auto process_value_or_object(auto &serializer, obj_or_value &&vo)
    -> std::enable_if_t<
        std::is_class_v<std::remove_reference_t<obj_or_value>>> {
  serializer->object(std::forward<obj_or_value>(vo));
}

template <typename obj_or_value>
auto process_value_or_object(auto &serializer, obj_or_value &&vo)
    -> std::enable_if_t<
        !std::is_class_v<std::remove_reference_t<obj_or_value>>> {
  serializer->template value<sizeof(obj_or_value)>(
      std::forward<obj_or_value>(vo));
}

/*
One must pick a socket type for "T", a later example will show a TCP example.
*/
template <typename socket_type> struct erpc_node;

template <> struct erpc_node<tcp_socket> {

  template <typename K> union un {
    K len;
    std::array<std::byte, sizeof(K)> bytes;
  };

  /*
    By default, a node should not serve calls.
    Parameter "ep" in the context of binding is a local address.
   */
  erpc_node(const endpoint ep, const int max_incoming_connections = 0) {
    if (max_incoming_connections) {
      internal.bind(ep);
      internal.listen(max_incoming_connections);
    }
  }

  ~erpc_node() { internal.close(); }

  void register_function(auto &function) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_args = decltype(arguments(function));
    using result_t = return_type<decltype(function)>;
    std::string func_name = std::string(typeid(decltype(function)).name());
    std::cerr << "Registered Function: " << func_name << std::endl;
    lookup.emplace(func_name, [function](tcp_socket *from, buffer &buf) {
      func_args arguments;
      {
        auto deserializer = std::unique_ptr<type_deserializer>(
            new type_deserializer{std::begin(buf), buf.size()});
        std::apply(
            [&deserializer](auto &&...vals) {
              (process_value_or_object(deserializer, vals), ...);
            },
            arguments);
      }

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments);
      } else {
        auto serializer =
            std::unique_ptr<type_serializer>(new type_serializer{buf});
        un<size_t> byte_len;
        auto result = std::apply(function, arguments);
        process_value_or_object(serializer, result);
        byte_len.len = serializer->adapter().writtenBytesCount();
        from->send(byte_len.bytes);
        buf.resize(byte_len.len);
        from->send(buf);
      }

      // return function so we can extract the type later.
      return function;
    });
  }

  /*
    Subscribe to a node, this allows you to execute functions on the device you
    subscribed to.

    Will return if it was successful or not.
   */
  bool subscribe(const endpoint e) {
    tcp_socket socket;
    socket.connect(e);
    providers.emplace_back(std::move(socket));
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
  template <typename... Args>
  auto call(tcp_socket *target, auto &function, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    auto iter = lookup.find(typeid(function).name());

    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    using result_t = std::invoke_result_t<decltype(function), Args...>;
    buffer buf;

    un<size_t> byte_len;
    auto serializer =
        std::unique_ptr<type_serializer>(new type_serializer{buf});

    serializer->text<sizeof(std::string::value_type)>(iter->first,
                                                      max_func_name_len);
    std::apply(
        [&serializer](auto &&...vals) {
          (process_value_or_object(serializer, vals), ...);
        },
        std::make_tuple(args...));

    byte_len.len = serializer->adapter().writtenBytesCount();
    target->send(byte_len.bytes);
    buf.resize(byte_len.len);
    target->send(buf);
    if constexpr (std::is_void_v<result_t>)
      return;

    target->receive_some(byte_len.bytes);
    buf.resize(byte_len.len);
    target->receive_some(buf);

    result_t return_val;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(buf), byte_len.len});
    process_value_or_object(deserializer, return_val);
    return return_val;
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, send. This function will also block until there is
    something to respond to.
   */
  void respond(tcp_socket *to) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    un<size_t> byte_len;
    buffer buf;

    to->receive_some(byte_len.bytes);
    buf.resize(byte_len.len);
    to->receive_some(buf);

    std::string func_name;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(buf), byte_len.len});

    deserializer->text<sizeof(std::string::value_type)>(func_name,
                                                        max_func_name_len);

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      std::cerr << "Function not registered: " << func_name << std::endl;

    auto func = iter->second;

    buf.erase(std::begin(buf),
              std::begin(buf) + deserializer->adapter().currentReadPos());
    (func)(to, buf);
    return;
  }

  std::unordered_map<
      std::string,
      std::function<void(tcp_socket *from, std::vector<std::byte> &buf)>>
      lookup;
  std::vector<tcp_socket> subscribers;
  std::vector<tcp_socket> providers;

  const size_t max_func_name_len = 64;
  tcp_socket internal;
};

template <> struct erpc_node<ssl_socket> {

  template <typename K> union un {
    K len;
    std::array<std::byte, sizeof(K)> bytes;
  };

  /*
    By default, a node should not serve calls.
    Parameter "ep" in the context of binding is a local address.
   */
  erpc_node(const endpoint ep, const int max_incoming_connections = 0) {
    if (max_incoming_connections) {
      internal.bind(ep);
      internal.listen(max_incoming_connections);
    }
  }

  ~erpc_node() { internal.close(); }

  void register_function(auto &function) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_args = decltype(arguments(function));
    using result_t = return_type<decltype(function)>;
    std::string func_name = std::string(typeid(decltype(function)).name());
    std::cerr << "Registered Function: " << func_name << std::endl;
    lookup.emplace(func_name, [function](ssl_socket *from, buffer &buf) {
      func_args arguments;
      {
        auto deserializer = std::unique_ptr<type_deserializer>(
            new type_deserializer{std::begin(buf), buf.size()});
        std::apply(
            [&deserializer](auto &&...vals) {
              (process_value_or_object(deserializer, vals), ...);
            },
            arguments);
      }

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments);
      } else {
        auto serializer =
            std::unique_ptr<type_serializer>(new type_serializer{buf});
        un<size_t> byte_len;
        auto result = std::apply(function, arguments);
        process_value_or_object(serializer, result);
        byte_len.len = serializer->adapter().writtenBytesCount();
        from->send(byte_len.bytes);
        buf.resize(byte_len.len);
        from->send(buf);
      }

      // return function so we can extract the type later.
      return function;
    });
  }

  /*
    Subscribe to a node, this allows you to execute functions on the device you
    subscribed to.

    Will return if it was successful or not.
   */
  bool subscribe(const endpoint e) {
    ssl_socket socket;
    socket.connect(e);
    providers.emplace_back(std::move(socket));
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
  template <typename... Args>
  auto call(ssl_socket *target, auto &function, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    auto iter = lookup.find(typeid(function).name());

    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    using result_t = std::invoke_result_t<decltype(function), Args...>;
    buffer buf;

    un<size_t> byte_len;
    auto serializer =
        std::unique_ptr<type_serializer>(new type_serializer{buf});

    serializer->text<sizeof(std::string::value_type)>(iter->first,
                                                      max_func_name_len);
    std::apply(
        [&serializer](auto &&...vals) {
          (process_value_or_object(serializer, vals), ...);
        },
        std::make_tuple(args...));

    byte_len.len = serializer->adapter().writtenBytesCount();
    target->send(byte_len.bytes);
    buf.resize(byte_len.len);
    target->send(buf);
    if constexpr (std::is_void_v<result_t>)
      return;

    target->receive_some(byte_len.bytes);
    buf.resize(byte_len.len);
    target->receive_some(buf);

    result_t return_val;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(buf), byte_len.len});
    process_value_or_object(deserializer, return_val);
    return return_val;
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, send. This function will also block until there is
    something to respond to.
   */
  void respond(ssl_socket *to) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    un<size_t> byte_len;
    buffer buf;

    to->receive_some(byte_len.bytes);
    buf.resize(byte_len.len);
    to->receive_some(buf);

    std::string func_name;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(buf), byte_len.len});

    deserializer->text<sizeof(std::string::value_type)>(func_name,
                                                        max_func_name_len);

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      std::cerr << "Function not registered: " << func_name << std::endl;

    auto func = iter->second;

    buf.erase(std::begin(buf),
              std::begin(buf) + deserializer->adapter().currentReadPos());
    (func)(to, buf);
    return;
  }

  std::unordered_map<
      std::string,
      std::function<void(ssl_socket *from, std::vector<std::byte> &buf)>>
      lookup;
  std::vector<ssl_socket> subscribers;
  std::vector<ssl_socket> providers;

  const size_t max_func_name_len = 64;
  ssl_socket internal;
};

// template <> struct erpc_node<udp_socket> {

//   template <typename K> union un {
//     K val;
//     std::array<std::byte, sizeof(K)> bytes;
//   };

//   struct rpc_header {
//     uint32_t seq_num; // starting from 0, etc.
//     uint32_t msg_len; // how many bytes after?
//   };

//   struct packet {
//     rpc_header header;
//     std::vector<std::byte> bytes;
//     bool operator()(const packet &lhs, const packet &rhs) const {
//       return lhs.header.seq_num < rhs.header.seq_num;
//     }
//   };

//   /*
//     By default, a node should not serve calls.
//     Parameter "ep" in the context of binding is a local address.
//    */
//   erpc_node(const endpoint ep, const int max_incoming_connections = 0) {
//     if (max_incoming_connections) {
//       internal.bind(ep);
//     }
//   }

//   ~erpc_node() { internal.close(); }

//   template <typename Func, typename... Args>
//   void register_function(std::string func_name, Func function) {
//     using buffer = std::vector<std::byte>;
//     using reader = bitsery::InputBufferAdapter<buffer>;
//     using writer = bitsery::OutputBufferAdapter<buffer>;

//     using type_serializer = bitsery::Serializer<writer>;
//     using type_deserializer = bitsery::Deserializer<reader>;

//     using result_t = std::invoke_result_t<Func, Args...>;
//     using func_args = std::tuple<Args...>;

//     lookup.emplace(func_name, [&function, self = &this->internal](
//                                   endpoint *from, buffer &buf) {
//       func_args arguments;
//       {
//         auto deserializer = std::unique_ptr<type_deserializer>(
//             new type_deserializer{std::begin(buf), buf.size()});

//         // TODO: verify that the deserializer is actually saving to the
//         // arguments tuple
//         std::apply(
//             [&deserializer](auto &&...vals) {
//               (deserializer->value<sizeof(vals)>(vals), ...);
//             },
//             arguments);
//         // deserializer->ext(arguments, bitsery::ext::StdTuple<Args...>{});
//       }

//       if constexpr (std::is_void_v<result_t>) {
//         std::apply(function, arguments);
//       } else {
//         auto serializer =
//             std::unique_ptr<type_serializer>(new type_serializer{buf});
//         un<rpc_header> header;
//         auto result = std::apply(function, arguments);

//         std::cout << "Request Result: " << result << std::endl;
//         serializer->value<sizeof(result)>(result);
//         // serializer->ext(result, bitsery::ext::StdTuple<result_t>{});
//         header.val.msg_len = serializer->adapter().writtenBytesCount();
//         //        header.val.seq_num = subscribers[*from]++;

//         self->send(header.bytes, *from);
//         self->send(buf, *from);
//       }
//     });
//   }

//   std::unordered_map<
//       std::string,
//       std::function<void(tcp_socket *from, std::vector<std::byte> &buf)>>
//       lookup;

//   std::unordered_map<endpoint, uint32_t> subscribers;
//   std::unordered_map<endpoint, uint32_t> providers;
//   const size_t max_func_name_len = 64;
//   std::priority_queue<packet, std::vector<packet>, std::less<packet>> pq;
//   udp_socket internal;
// };

#endif
