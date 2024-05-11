#ifndef NETVAR_HPP
#define NETVAR_HPP

#include <string>
#include <unordered_map>
#include <utility>
#include <uuid/uuid.h>
#include <vector>

#include "endpoint.hpp"
#include "rpc_node.hpp"
#include "singleton.hpp"

template <typename SocketType, typename T> struct netvar;

namespace netvar_ns {

template <typename SocketType, typename T>
int update_variable(T v, std::string uuid) {
  auto &lookup = singleton<
      std::unordered_map<std::string, netvar<SocketType, T> *>>::instance();

  auto iter = lookup.find(uuid);
  if (iter != std::end(lookup))
    iter->second->var = v;
  else
    std::cerr << "Unable to find ID: " << uuid << std::endl;
  return 0;
}

template <typename SocketType, typename T>
std::string instantiate_variable(T v) {
  unsigned char uuid[16];
  uuid_generate_random(uuid);
  std::string strid(reinterpret_cast<char *>(uuid), sizeof(uuid));

  netvar<SocketType, T> *sv = new netvar<SocketType, T>(v, false);
  sv->id = strid;
  auto &lookup = singleton<
      std::unordered_map<std::string, netvar<SocketType, T> *>>::instance();

  lookup.emplace(strid, sv);

  std::cerr << "Instantiated Variable: " << strid << std::endl;
  return strid;
}

template <typename SocketType, typename T> T delete_variable(std::string uuid) {
  auto &lookup = singleton<
      std::unordered_map<std::string, netvar<SocketType, T> *>>::instance();

  auto iter = lookup.find(uuid);
  if (iter != std::end(lookup)) {
    delete iter->second; // netvar deconstructor removes itself from the
                         // lookup table.
  } else
    std::cerr << "Unable to find ID: " << uuid << std::endl;

  T trash;
  return trash;
}

}; // namespace netvar_ns

template <typename SocketType, typename... Types>
struct netvar_service : erpc_node<SocketType> {
  netvar_service(const endpoint &ep, const int max_incoming_connections = 0)
      : erpc_node<SocketType>(ep, max_incoming_connections) {
    ((erpc_node<SocketType>::register_function(
         netvar_ns::instantiate_variable<SocketType, Types>)),
     ...);
    ((erpc_node<SocketType>::register_function(
         netvar_ns::delete_variable<SocketType, Types>)),
     ...);
    ((erpc_node<SocketType>::register_function(
         netvar_ns::update_variable<SocketType, Types>)),
     ...);

    singleton<erpc_node<SocketType> *>::instance() = this;
  }
};

template <typename SocketType, typename T> struct netvar {

  // implicit getter.
  operator const T &() const { return var; }
  // const T &get() { return var; }

  // implicit setter.
  netvar<SocketType, T> &operator=(const T &obj) {
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

    auto &service = singleton<erpc_node<SocketType> *>::instance();

    // set for remotes upstream.
    for (auto &provider : service->providers)
      service->call(&provider, netvar_ns::update_variable<SocketType, T>, obj,
                    id);

    // set for remotes downstream.
    for (auto &subscriber : service->subscribers)
      service->call(&subscriber, netvar_ns::update_variable<SocketType, T>, obj,
                    id);

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

      auto &service = singleton<erpc_node<SocketType> *>::instance();
      auto &lookup = singleton<
          std::unordered_map<std::string, netvar<SocketType, T> *>>::instance();

      for (auto &provider : service->providers)
        id = service->call(&provider,
                           netvar_ns::instantiate_variable<SocketType, T>,
                           this->var);

      lookup.emplace(id, this);
    }
  }

  // TODO: implement ownership functionality. and a way to verify the
  // authenticity of the request.

  ~netvar() {
    auto &service = singleton<erpc_node<SocketType> *>::instance();
    auto &lookup = singleton<
        std::unordered_map<std::string, netvar<SocketType, T> *>>::instance();

    if (local) {
      for (auto &provider : service->providers)
        service->call(&provider, netvar_ns::delete_variable<SocketType, T>, id);
    }

    auto iter = lookup.find(id);
    if (iter != std::end(lookup)) {
      lookup.erase(iter);
    }
  }

  T var;
  std::string id;
  bool local;
};

#endif
