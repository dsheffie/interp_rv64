#ifndef __fdthh__
#define __fdthh__

int riscv_build_fdt(uint8_t *dst,
		    uint64_t kernel_start, uint64_t kernel_size,
		    uint64_t initrd_start, uint64_t initrd_size,
		    const char *cmd_line);

#endif
