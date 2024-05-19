#include "uart.hh"
#include "temu_code.hh"
#include "interpret.hh"

#define U8250_INT_THRE 1


uart::uart(state_t *s) : s(s), dll(0), dlh(0), lcr(0), ier(0), current_int(0), pending_ints(0), mcr(0), in_ready(0) {}


bool uart::handle(uint64_t addr, bool store, int64_t st_data) {
  uint64_t offs = addr - UART_BASE_ADDR;
  //printf("accessing offset %lx into uart space, store %d, pc %lx\n",
  //offs, store, s->pc);

  
  if(store) {
    int8_t value = *reinterpret_cast<int8_t*>(s->mem + addr);
    switch (offs)
      {
      case 0:
	//printf("write to reg 0, %c\n", value);
	 
	if (lcr & (1 << 7)) { /* DLAB */
	  dll = value;
	  break;
	}
	//u8250_handle_out(uart, value);
	//printf(">>> %c", value);
	pending_ints |= 1 << U8250_INT_THRE;
	break;
      case 1:
	if (lcr & (1 << 7)) { /* DLAB */
	  dlh = value;
	  break;
	}
	ier = value;
	break;
      case 3:
	lcr = value;
	printf(">> lcr set to %x at pc %lx\n", lcr, s->pc);
	
        break;
      case 4:
	mcr = value;
	break;
      default:
	break;
      }
    return true;
  }
  else {
    int8_t *value = reinterpret_cast<int8_t*>(s->mem + addr);    
    switch (offs)
      {
      case 0:
	if (lcr & (1 << 7)) { /* DLAB */
	  *value = dll;
	  break;
        }
        //*value = u8250_handle_in(uart);
        break;
      case 1:
	if (lcr & (1 << 7)) { /* DLAB */
	  *value = dlh;
	  break;
	}
	*value = ier;
        break;
      case 2:
        *value = (current_int << 1) | (pending_ints ? 0 : 1);
        if (current_int == U8250_INT_THRE) {
	  pending_ints &= ~(1 << current_int);
	}
        break;
    case 3:
        *value = lcr;
        break;
    case 4:
        *value = mcr;
        break;
    case 5:
        /* LSR = no error, TX done & ready */
        *value = 0x60 | in_ready;
        break;
    case 6:
        /* MSR = carrier detect, no ring, data ready, clear to send. */
        *value = 0xb0;
        break;
        /* no scratch register, so we should be detected as a plain 8250. */
      default:
        *value = 0;
      }
  }
  

  return true;
}

void uart::update_irq() {
  if(in_ready) {
    pending_ints |= 1;
  }
  else {
    pending_ints &= ~1;
  }

  /* Prevent generating any disabled interrupts in the first place */
  pending_ints &= ier;
  
  /* Update current interrupt (higher bits -> more priority) */
  if (pending_ints) {
    assert(pending_ints == 1);
    current_int = 0;//ilog2(uart->pending_ints);
    //printf("uart irq pending ..\n");
  }
}
