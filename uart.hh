#ifndef __UART_HH__
#define __UART_HH__

#include <cstdint>

struct state_t;

struct uart {
  state_t *s;

  /* stolen from https://github.com/sysprog21/semu */
  uint8_t dll, dlh;                  /**< divisor (ignored) */
  uint8_t lcr;                       /**< UART config */
  uint8_t ier;                       /**< interrupt config */
  uint8_t current_int, pending_ints; /**< interrupt status */
  /* other output signals, loopback mode (ignored) */
  uint8_t mcr, in_ready;


  /* I/O handling */
  int in_fd, out_fd;  
  
  uart(state_t *s);
  bool handle(uint64_t addr, bool store, int64_t st_data);
  void update_irq();
};

#endif
