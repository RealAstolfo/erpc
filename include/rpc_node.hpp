#ifndef ERPC_RPC_NODE_HPP
#define ERPC_RPC_NODE_HPP

#include <array>
#include <cstddef>
#include <cxxabi.h>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <md4.h>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/ext/std_tuple.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include "bitsery/deserializer.h"
#include "bitsery/serializer.h"

#include "endpoint.hpp"
#include "function_helpers.hpp"
#include "http.hpp"
#include "ssl.hpp"
#include "tcp.hpp"
#include "udp.hpp"

template <typename T> struct is_optional : std::false_type {};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T> constexpr bool is_optional_v = is_optional<T>::value;

template <typename Serializer, typename T>
auto process_value_or_object(Serializer &serializer, T &&value)
    -> std::enable_if_t<
        std::is_same_v<std::remove_reference_t<T>, std::string>> {
  serializer->template text<sizeof(std::string::value_type)>(
      std::forward<T>(value), std::numeric_limits<std::size_t>::max());
}

template <typename Serializer, typename T>
auto process_value_or_object(Serializer &serializer, T &&value)
    -> std::enable_if_t<is_optional_v<std::remove_reference_t<T>>> {
  serializer->ext(std::forward<T>(value), bitsery::ext::StdOptional{});
}

template <typename Serializer, typename T>
auto process_value_or_object(Serializer &serializer, T &&value)
    -> std::enable_if_t<
        !std::is_same_v<std::remove_reference_t<T>, std::string> &&
        !is_optional_v<std::remove_reference_t<T>> &&
        std::is_class_v<std::remove_reference_t<T>>> {
  serializer->object(std::forward<T>(value));
}

template <typename Serializer, typename T>
auto process_value_or_object(Serializer &serializer, T &&value)
    -> std::enable_if_t<
        !std::is_same_v<std::remove_reference_t<T>, std::string> &&
        !std::is_class_v<std::remove_reference_t<T>>> {
  serializer->template value<sizeof(T)>(std::forward<T>(value));
}

constexpr std::string demangle(const std::string &type) {
  int status;
  char *realname;

  realname = abi::__cxa_demangle(type.c_str(), NULL, NULL, &status);
  std::string func_name = std::string(realname);
  free(realname);
  return func_name;
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
    bind(ep, max_incoming_connections);
  }

  ~erpc_node() { internal.close(); }

  void bind(const endpoint ep, const int max_incoming_connections = 0) {
    if (max_incoming_connections) {
      internal.bind(ep);
      internal.listen(max_incoming_connections);
    }
  }

  void register_function(auto &function) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_args = decltype(arguments_t(function));
    using result_t = decltype(return_t(function));
    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());

    std::cerr << "Function Name: " << func_name << std::endl;

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));

    std::cerr << "Registered Function: " << md4hash << std::endl;
    lookup.emplace(md4hash, [function](tcp_socket *from, buffer &buf) {
      func_args arguments_t;
      {
        auto deserializer = std::unique_ptr<type_deserializer>(
            new type_deserializer{std::begin(buf), buf.size()});
        std::apply(
            [&deserializer](auto &&...vals) {
              (process_value_or_object(deserializer, vals), ...);
            },
            arguments_t);
      }

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments_t);
      } else {
        auto serializer =
            std::unique_ptr<type_serializer>(new type_serializer{buf});
        un<size_t> byte_len;
        auto result = std::apply(function, arguments_t);
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

    Internally, it will serialize the arguments_t and call on the target remote.
   */
  template <typename... Args>
  auto call(tcp_socket *target, auto &function, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));
    auto iter = lookup.find(md4hash);

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
    else {
      target->receive_some(byte_len.bytes);
      buf.resize(byte_len.len);
      target->receive_some(buf);

      result_t return_val;
      auto deserializer = std::unique_ptr<type_deserializer>(
          new type_deserializer{std::begin(buf), byte_len.len});
      process_value_or_object(deserializer, return_val);
      return return_val;
    }
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

  const size_t max_func_name_len = 65535;
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

    using func_args = decltype(arguments_t(function));
    using result_t = decltype(return_t(function));
    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());
    std::cerr << "Function Name: " << func_name << std::endl;

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));

    std::cerr << "Registered Function: " << md4hash << std::endl;
    lookup.emplace(md4hash, [function](ssl_socket *from, buffer &buf) {
      func_args arguments_t;
      {
        auto deserializer = std::unique_ptr<type_deserializer>(
            new type_deserializer{std::begin(buf), buf.size()});
        std::apply(
            [&deserializer](auto &&...vals) {
              (process_value_or_object(deserializer, vals), ...);
            },
            arguments_t);
      }

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments_t);
      } else {
        auto serializer =
            std::unique_ptr<type_serializer>(new type_serializer{buf});
        un<size_t> byte_len;
        auto result = std::apply(function, arguments_t);
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

    Internally, it will serialize the arguments_t and call on the target remote.
   */
  template <typename... Args>
  auto call(ssl_socket *target, auto &function, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));

    auto iter = lookup.find(md4hash);

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

  const size_t max_func_name_len = 65535;
  ssl_socket internal;
};

template <> struct erpc_node<http_socket> {

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

    using func_args = decltype(arguments_t(function));
    using result_t = decltype(return_t(function));
    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());
    std::cerr << "Function Name: " << func_name << std::endl;

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));

    std::cerr << "Registered Function: " << md4hash << std::endl;
    lookup.emplace(md4hash, [function](http_socket *from, buffer &buf) {
      func_args arguments_t;
      {
        auto deserializer = std::unique_ptr<type_deserializer>(
            new type_deserializer{std::begin(buf), std::size(buf)});
        std::apply(
            [&deserializer](auto &&...vals) {
              (process_value_or_object(deserializer, vals), ...);
            },
            arguments_t);
      }

      if constexpr (std::is_void_v<result_t>) {
        std::apply(function, arguments_t);
      } else {
        buf.clear();
        auto serializer =
            std::unique_ptr<type_serializer>(new type_serializer{buf});
        auto result = std::apply(function, arguments_t);
        process_value_or_object(serializer, result);
        from->respond(buf);
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
    http_socket socket;
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

    Internally, it will serialize the arguments_t and call on the target remote.
   */
  template <typename... Args>
  auto call(http_socket *target, auto &function, Args &&...args) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using writer = bitsery::OutputBufferAdapter<buffer>;

    using type_serializer = bitsery::Serializer<writer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    using func_sig = decltype(signature_t(function));
    std::string func_name = demangle(typeid(func_sig).name());

    MD4_CTX md4ctx;
    MD4Init(&md4ctx);
    MD4Update(&md4ctx, (const uint8_t *)func_name.c_str(), func_name.length());
    char hash[MD4_DIGEST_STRING_LENGTH] = {0};
    MD4End(&md4ctx, hash);
    std::string md4hash(std::string_view(hash, strlen(hash)));
    auto iter = lookup.find(md4hash);

    if (iter == std::end(lookup))
      throw std::runtime_error("Function not registered");

    using result_t = std::invoke_result_t<decltype(function), Args...>;
    buffer buf;

    auto serializer =
        std::unique_ptr<type_serializer>(new type_serializer{buf});

    serializer->text<sizeof(std::string::value_type)>(iter->first,
                                                      max_func_name_len);
    std::apply(
        [&serializer](auto &&...vals) {
          (process_value_or_object(serializer, vals), ...);
        },
        std::make_tuple(args...));

    buffer receive = target->request<buffer, buffer>(buf);
    if constexpr (std::is_void_v<result_t>)
      return;

    result_t return_val;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(receive), std::size(receive)});
    process_value_or_object(deserializer, return_val);
    return return_val;
  }

  /*
    This function will pull a call from the network, deserialize it, execute,
    serialize result, post. This function will also block until there is
    something to respond to.
   */
  void respond(http_socket *to) {
    using buffer = std::vector<std::byte>;
    using reader = bitsery::InputBufferAdapter<buffer>;
    using type_deserializer = bitsery::Deserializer<reader>;

    buffer buf;
    to->receive(buf);

    std::string func_name;
    auto deserializer = std::unique_ptr<type_deserializer>(
        new type_deserializer{std::begin(buf), std::size(buf)});

    deserializer->text<sizeof(std::string::value_type)>(func_name,
                                                        max_func_name_len);

    auto iter = lookup.find(func_name);
    if (iter == std::end(lookup))
      std::cerr << "Function not registered: " << func_name << std::endl;

    auto func = iter->second;

    buf.erase(std::begin(buf),
              std::begin(buf) + deserializer->adapter().currentReadPos());
    // buf is modified.
    (func)(to, buf);
    return;
  }

  std::unordered_map<
      std::string,
      std::function<void(http_socket *from, std::vector<std::byte> &buf)>>
      lookup;
  std::vector<http_socket> subscribers;
  std::vector<http_socket> providers;

  const size_t max_func_name_len = 65535;
  http_socket internal;
};

#endif
