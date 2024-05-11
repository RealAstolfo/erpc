#ifndef NETVAR_HPP
#define NETVAR_HPP

#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <uuid/uuid.h>
#include <vector>

#include "endpoint.hpp"
#include "rpc_node.hpp"

template <typename T, typename SocketType> struct netvar;

namespace netvar_ns {

template <typename T, typename SocketType>
int update_variable(T v, std::string uuid) {
  auto iter = netvar<T, SocketType>::lookup.find(uuid);
  if (iter != std::end(netvar<T, SocketType>::lookup))
    iter->second->var = v;
  else
    std::cerr << "Unable to find ID: " << uuid << std::endl;
  return 0;
}

template <typename T, typename SocketType>
std::string instantiate_variable(T v) {
  unsigned char uuid[16];
  uuid_generate_random(uuid);
  std::string strid(reinterpret_cast<char *>(uuid), sizeof(uuid));

  netvar<T, SocketType> *sv = new netvar<T, SocketType>(v, false);
  sv->id = strid;

  netvar<T, SocketType>::lookup.emplace(strid, sv);

  std::cerr << "Instantiated Variable: " << strid << std::endl;
  return strid;
}

template <typename T, typename SocketType>
int delete_variable(std::string uuid) {
  auto iter = netvar<T, SocketType>::lookup.find(uuid);
  if (iter != std::end(netvar<T, SocketType>::lookup)) {
    delete iter
        ->second; // netvar deconstructor removes itself from the lookup table.
  } else
    std::cerr << "Unable to find ID: " << uuid << std::endl;

  return 0;
}

}; // namespace netvar_ns

template <typename T, typename SocketType> struct netvar {

  struct netvar_service : erpc_node<SocketType> {
    netvar_service(const endpoint &ep, const int max_incoming_connections = 0)
        : erpc_node<SocketType>(ep, max_incoming_connections) {
      erpc_node<SocketType>::register_function(
          netvar_ns::instantiate_variable<T, SocketType>);
      erpc_node<SocketType>::register_function(
          netvar_ns::delete_variable<T, SocketType>);
      erpc_node<SocketType>::register_function(
          netvar_ns::update_variable<T, SocketType>);
    }
  };

  // implicit getter.
  operator const T &() const { return var; }
  // const T &get() { return var; }

  // implicit setter.
  netvar<T, SocketType> &operator=(const T &obj) {
    // void set(const T &obj) {

    // set for self.
    if (local) {
      var = obj;
    }
    // The following MAY cause a broadcast storm. make sure to verify this.
    // EDIT: just logically i can see that this would definitely cause a
    // broadcast storm, so ill have to implement a "broadcast" function in the
    // erpc_node, which allows users to broadcast to the rest of the network in
    // a way where it does not also return to sender.
    // NOTE: would this also be reasoning to introduce meshing? (making
    // software-routable ID's to increase effectiveness of synchronization,
    // reduce chances of broadcast looping, capability of multicasting.

    // set for remotes upstream.
    for (auto &provider : netvar_interface->providers)
      netvar_interface->call(
          &provider, netvar_ns::update_variable<T, SocketType>, obj, id);

    // set for remotes downstream.
    for (auto &subscriber : netvar_interface->subscribers)
      netvar_interface->call(
          &subscriber, netvar_ns::update_variable<T, SocketType>, obj, id);

    return *this;
  }

  netvar(T &var, bool local = true) {
    this->local = local;
    this->var = std::move(var);

    // TODO: authorities need to be determined. i believe it should be the
    // users' authority for its own local stuff. figuring out enforcing
    // different restrictions are to be determined at a higher level. Ex. player
    // shouldnt directly control their position. but should absolutely control
    // their keyboard/mouse inputs.
    if (local) {
      for (auto &provider : netvar_interface->providers)
        id = netvar_interface->call(
            &provider, netvar_ns::instantiate_variable<T, SocketType>,
            this->var);

      netvar<T, SocketType>::lookup.emplace(id, this);
    }
  }

  // TODO: implement ownership functionality. and a way to verify the
  // authenticity of the request.

  ~netvar() {
    if (local) {
      for (auto &provider : netvar_interface->providers)
        netvar_interface->call(&provider,
                               netvar_ns::delete_variable<T, SocketType>, id);
    }

    auto iter = lookup.find(id);
    if (iter != std::end(lookup)) {
      lookup.erase(iter);
    }
  }

  static std::unordered_map<std::string, netvar<T, SocketType> *> lookup;
  static netvar_service *netvar_interface;

  T var;
  std::string id;
  bool local;
};

#endif
