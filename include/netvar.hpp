#ifndef NETVAR_HPP
#define NETVAR_HPP

#include <cstring>
#include <stdexcept>
#include <string>
#include <uuid/uuid.h>

#include "rpc_node.hpp"
#include "tcp.hpp"

template <typename T> struct netvar {
  static erpc_node<tcp_socket> *netvar_transport;

  // implicit getter.
  T &operator=(const netvar<T> &net_obj) { return net_obj.obj; }

  // implicit setter.
  netvar<T> &operator=(const T &obj) {
    this->obj = obj;
    modified = true;
  }

  netvar(T &object) {
    if (netvar_transport == nullptr)
      throw std::runtime_error("netvar_transport has not been initialized");

    id = netvar_transport->call(
        netvar_transport->providers[0], []() -> std::string {
          char *id;
          uuid_generate_random(reinterpret_cast<unsigned char *>(id));
          std::string strid(id, strlen(id));
          return strid;
        });

    obj = object;
    modified = false;
  }

private:
  T obj;
  std::string id;
  bool modified;
  bool is_local;
};

#endif
