#include "virtio.hh"
#include "temu_code.hh"
#include "interpret.hh"

virtio::virtio(state_t *s) : s(s) {}

bool virtio::handle(uint64_t addr, bool store, int64_t st_data) {
  uint64_t offs = addr - VIRTIO_BASE_ADDR;
  printf("accessing offset %lx into virtio space, store %d, pc %lx\n",
	 offs, store, s->pc);
  switch(offs)
    {
    case 0: /* mmio magic value */
      assert(not(store));
      *reinterpret_cast<int*>(s->mem + addr) = 0x74726976;
      break;
    case 4: /* mmio version */
      assert(not(store));
      *reinterpret_cast<int*>(s->mem + addr) = 2;
      break;
    case 8: /* device id */
      assert(not(store));
      *reinterpret_cast<int*>(s->mem + addr) = 0x1003;
      break;
    case 12: /* vendor id */
      assert(not(store));
      *reinterpret_cast<int*>(s->mem + addr) = 0xffff;
      break;
    case 112: /* mmio status */
      if(store) {
	printf("mmio status reg write %lx\n", st_data);
	*reinterpret_cast<int*>(s->mem + addr) = st_data;
      }
      break;
    default:
      return false;
    }
  return true;
}
