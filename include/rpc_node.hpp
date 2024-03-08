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
#include "network_buffer.hpp"
#include "tcp.hpp"
#include "udp.hpp"

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

  template <typename... Args>
  void register_function(std::string func_name, auto function) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using result_t = std::invoke_result_t<decltype(function), Args...>;
    using func_args = std::tuple<Args...>;

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
  template <typename Func, typename... Args>
  std::invoke_result_t<Func, Args...>
  call(tcp_socket *target, std::string func_name, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using return_t = std::invoke_result_t<Func, Args...>;

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    buffer buf;

    un<size_t> byte_len;
    auto serializer =
        std::unique_ptr<type_serializer>(new type_serializer{buf});

    serializer->text<sizeof(std::string::value_type)>(func_name,
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
    if constexpr (std::is_void_v<return_t>)
      return;

    target->receive_some(byte_len.bytes);
    buf.resize(byte_len.len);
    target->receive_some(buf);

    return_t return_val;
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
    func(to, buf);
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
