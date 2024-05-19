#ifndef __UART_HH__
#define __UART_HH__

#include <cstdint>

struct state_t;

struct uart {
  state_t *s;
  uart(state_t *s);
  bool handle(uint64_t addr, bool store, int64_t st_data);
};

#endif
