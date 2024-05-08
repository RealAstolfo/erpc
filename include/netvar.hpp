#ifndef NETVAR_HPP
#define NETVAR_HPP

#include <cstring>
#include <stdexcept>
#include <string>
#include <uuid/uuid.h>

#include "rpc_node.hpp"
#include "tcp.hpp"

template <typename T> struct netvar : T {
  erpc_node<tcp_socket> *netvar_transport = nullptr;

  // implicit getter.
  T &operator=(const netvar<T> &net_obj) { return net_obj.obj; }

  // implicit setter.
  netvar<T> &operator=(const T &obj) {

    // set for self.
    this->obj = obj;

    // The following MAY cause a broadcast storm. make sure to verify this.
    // EDIT: just logically i can see that this would definitely cause a
    // broadcast storm, so ill have to implement a "broadcast" function in the
    // erpc_node, which allows users to broadcast to the rest of the network in
    // a way where it does not also return to sender.
    // NOTE: would this also be reasoning to introduce meshing? (making
    // software-routable ID's to increase effectiveness of synchronization,
    // reduce chances of broadcast looping, capability of multicasting.

    // set for remotes.
    for (auto &provider : netvar_transport->providers)
      netvar_transport->call(
          provider, [this](T obj) { this->obj = obj; }, obj);

    // set for remotes.
    for (auto &subscriber : netvar_transport->subscribers)
      netvar_transport->call(
          subscriber, [this](T obj) { this->obj = obj; }, obj);
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

    *this = object;
    modified = false;
  }

private:
  std::string id;
  bool modified;
  bool is_local;
};

#endif
