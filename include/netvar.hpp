#ifndef NETVAR_HPP
#define NETVAR_HPP

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <uuid/uuid.h>
#include <vector>

#include "rpc_node.hpp"

template <typename T, typename SocketType> struct netvar {
  static erpc_node<SocketType> netvar_transport;
  static std::unordered_map<std::string, netvar<T, SocketType> *> netvar_lookup;

  // implicit getter.
  // T &operator=(const netvar<T> &net_obj) { return net_obj.obj; }

  // implicit setter.
  // netvar<T> &operator=(const T &obj) {
  void set(const T &obj) {

    // set for self.
    if (is_local) {
      this->obj = obj;
      modified = true;
    }
    // The following MAY cause a broadcast storm. make sure to verify this.
    // EDIT: just logically i can see that this would definitely cause a
    // broadcast storm, so ill have to implement a "broadcast" function in the
    // erpc_node, which allows users to broadcast to the rest of the network in
    // a way where it does not also return to sender.
    // NOTE: would this also be reasoning to introduce meshing? (making
    // software-routable ID's to increase effectiveness of synchronization,
    // reduce chances of broadcast looping, capability of multicasting.

    const auto update_obj = [this](T obj, std::string uuid) {
      auto iter = netvar_lookup.find(uuid);
      if (iter != std::end(netvar_lookup) && !is_local)
        this->obj = obj;
    };
    // set for remotes upstream.
    for (auto &provider : netvar_transport.providers)
      netvar_transport.call(provider, update_obj, obj);

    // set for remotes downstream.
    for (auto &subscriber : netvar_transport.subscribers)
      netvar_transport.call(subscriber, update_obj, obj);
  }

  netvar(T &object) {
    is_local = true;
    modified = false;
    this->object = std::move(object);

    // TODO: authorities need to be determined. i believe it should be the
    // users' authority for its own local stuff. figuring out enforcing
    // different restrictions are to be determined at a higher level. Ex. player
    // shouldnt directly control their position. but should absolutely control
    // their keyboard/mouse inputs.
    id = netvar_transport.call(
        netvar_transport.providers[0],
        [](T obj) -> std::string {
          char *id;
          uuid_generate_random(reinterpret_cast<unsigned char *>(id));
          std::string strid(id, strlen(id));

          netvar<T, SocketType> *nv =
              (netvar<T, SocketType> *)malloc(sizeof(netvar<T, SocketType>));
          nv->object = std::move(obj);
          nv->is_local = false;
          nv->modified = false;
          nv->id = strid;

          netvar<T, SocketType>::netvar_lookup.emplace(strid, std::move(nv));
          return strid;
        },
        this->object);

    netvar<T, SocketType>::netvar_lookup.emplace(id, this);
  }

  // TODO: implement ownership functionality. and a way to verify the
  // authenticity of the request.

  ~netvar() {
    netvar_transport.call(
        netvar_transport.providers[0],
        [this](std::string id) {
          auto iter = netvar_lookup.find(id);
          if (iter != std::end(netvar_lookup)) {
            free(*iter);
            std::erase(iter);
          }
        },
        id);
  }

private:
  T object;
  std::string id;
  bool modified;
  bool is_local;
};

#endif
