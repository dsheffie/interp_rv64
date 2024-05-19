#ifndef __VIRTIO_HH__
#define __VIRTIO_HH__

#include <cstdint>

struct state_t;

struct virtio {
  state_t *s;
  virtio(state_t *s);
  bool handle(uint64_t addr, bool store, int64_t st_data);
};


#endif
