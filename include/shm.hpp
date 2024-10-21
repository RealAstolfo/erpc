#ifndef ENET_SHM_HPP
#define ENET_SHM_HPP

#include "udp.hpp"

#include <cstddef>
#include <cstdlib>
#include <limits>

template <typename T> struct shm_ptr {

  const T *internal;
};

template <typename T, std::size_t Size> struct shm_allocator {
  enum message_type {
    usage = 0,
    nalloc = 1,
    nfree = 2,
    get = 3,
    set = 4
  }

  typedef T value_type;

  shm_allocator() {
    udp_resolver resolver;
    const endpoint e = resolver.resolve("0.0.0.0", "45458").front();
    cluster_service.bind(e);
  }

  ~shm_allocator() { cluster_service.close(); }

  template <typename K, std::size_t S>
  constexpr shm_allocator(const shm_allocator<K, S> &) noexcept {}

  [[nodiscard]] T *allocate(std::size_t n) {

    if (n > (Size - usage) / sizeof(T)) {
      for (auto &peer : peers) {
        std::size_t peer_usage;
        cluster_service.receive_into(usage, peer);

        // everyone in cluster is full, fail out for now.
        if (n > (Size - peer_usage) / sizeof(T)) {
          throw std::bad_array_new_length();
        }
      }
    }

    if (auto p = static_cast<T *>(std::malloc(n * sizeof(T)))) {
      return p;
    }

    throw std::bad_alloc();
  }

  void deallocate(T *p, std::size_t n) noexcept { std::free(p); }

  shm_ptr<T> nalloc(std::size_t size) {}

  std::vector<std::size_t> report_usage() {
    for (auto &peer : peers)
      cluster_service.send(usage, peer, 0);
  }

  std::size_t usage = 0;
  udp_socket cluster_service;
  std::vector<endpoint> peers;
};

#endif
