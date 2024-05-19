#include "uart.hh"
#include "temu_code.hh"
#include "interpret.hh"

uart::uart(state_t *s) : s(s) {}

bool uart::handle(uint64_t addr, bool store, int64_t st_data) {
  uint64_t offs = addr - UART_BASE_ADDR;
  printf("accessing offset %lx into uart space, store %d, pc %lx\n",
	 offs, store, s->pc);
  return true;
}
