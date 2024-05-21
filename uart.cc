#include "uart.hh"
#include "temu_code.hh"
#include "interpret.hh"

#define U8250_INT_THRE 1

/* definitions stolen from qemu */
#define UART_LCR_DLAB 0x80
#define UART_MCR_OUT2 0x08
#define UART_LSR_TX_EMPTY (1<<5)

uart::uart(state_t *s) : s(s), dll(0), dlh(0), lcr(0), ier(0), current_int(0), pending_ints(0), mcr(UART_MCR_OUT2),
			 in_ready(0) {}


bool uart::handle(uint64_t addr, bool store, int64_t st_data) {
  uint64_t offs = addr - UART_BASE_ADDR;
  if(not(store))
    //printf("accessing offset %lx into uart space, store %d, pc %lx, %x\n",
    //offs, store, s->pc, st_data);
  
  if(store) {
    switch (offs)
      {
      case 0:
	//printf("write to reg 0, %c\n", value);
	if (lcr & UART_LCR_DLAB) { /* DLAB */
	  dll = st_data;
	  break;
	}
	//printf("accessing offset %lx into uart space, store %d, pc %lx, %x\n",
	//offs, store, s->pc, st_data);
	
	//u8250_handle_out(uart, value);
	//printf("%c", st_data);
	pending_ints |= 1 << U8250_INT_THRE;
	break;
      case 1:
	if (lcr & UART_LCR_DLAB) { /* DLAB */
	  dlh = st_data;
	  break;
	}
	ier = st_data;
	break;
      case 3:
	lcr = st_data;
	break;
      case 4:
	mcr = st_data;
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
	if (lcr & UART_LCR_DLAB) {
	  *value = dll;
	  break;
        }
        //*value = u8250_handle_in(uart);
        break;
      case 1:
	if (lcr & UART_LCR_DLAB) {
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
	//dump_calls();
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
    //printf("uart irq pending ..\n");
    //assert(pending_ints == 1);
    current_int = 0;//ilog2(uart->pending_ints);

    //csr_t cc(0);
    //cc.mip.seip = 1;
    //s->mip |= cc.raw;
  }
}
