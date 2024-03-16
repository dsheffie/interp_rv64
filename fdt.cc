/*
 * RISCV machine
 * 
 * Copyright (c) 2016-2017 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdarg>

#include "helper.hh"

#define TEMU_JUST_DEFINES
#include "temu_code.hh"

#define RTC_FREQ 10000000

/* arbitrary, relative to CPU freq to have a 10 MHz frequency */
#define RTC_FREQ_DIV 16 

/* fdt and general code stolen from tinyemu */
/* FDT machine description */

#define FDT_MAGIC       0xd00dfeed
#define FDT_VERSION     17

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version; /* <= 17 */
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
       uint64_t address;
       uint64_t size;
};

#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

typedef struct {
    uint32_t *tab;
    int tab_len;
    int tab_size;
    int open_node_count;

    char *string_table;
    int string_table_len;
    int string_table_size;
} FDTState;

static FDTState *fdt_init(void)
{
  FDTState *f = reinterpret_cast<FDTState*>(malloc(sizeof(FDTState)));
  memset(f, 0, sizeof(FDTState));
  return f;
}

static void fdt_alloc_len(FDTState *s, int len)
{
  int new_size;
  if (len > s->tab_size) {
    new_size = std::max(len, s->tab_size * 3 / 2);
    s->tab = reinterpret_cast<uint32_t*>(realloc(s->tab, new_size * sizeof(uint32_t)));
    s->tab_size = new_size;
  }
}

static void fdt_put32(FDTState *s, int v)
{
    fdt_alloc_len(s, s->tab_len + 1);
    s->tab[s->tab_len++] = bswap<false>(v);
}


/* the data is zero padded */
static void fdt_put_data(FDTState *s, const uint8_t *data, int len)
{
    int len1;

    len1 = (len + 3) / 4;
    fdt_alloc_len(s, s->tab_len + len1);
    memcpy(s->tab + s->tab_len, data, len);
    memset((uint8_t *)(s->tab + s->tab_len) + len, 0, -len & 3);
    s->tab_len += len1;
}

static void fdt_begin_node(FDTState *s, const char *name)
{
    fdt_put32(s, FDT_BEGIN_NODE);
    fdt_put_data(s, (uint8_t *)name, strlen(name) + 1);
    s->open_node_count++;
}

static void fdt_begin_node_num(FDTState *s, const char *name, uint64_t n)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s@%" PRIx64, name, n);
    fdt_begin_node(s, buf);
}

static void fdt_end_node(FDTState *s)
{
    fdt_put32(s, FDT_END_NODE);
    s->open_node_count--;
}

static int fdt_get_string_offset(FDTState *s, const char *name)
{
    int pos, new_size, name_size, new_len;

    pos = 0;
    while (pos < s->string_table_len) {
        if (!strcmp(s->string_table + pos, name))
            return pos;
        pos += strlen(s->string_table + pos) + 1;
    }
    /* add a new string */
    name_size = strlen(name) + 1;
    new_len = s->string_table_len + name_size;
    if (new_len > s->string_table_size) {
      new_size = std::max(new_len, s->string_table_size * 3 / 2);
      s->string_table = reinterpret_cast<char*>(realloc(s->string_table, new_size));
      s->string_table_size = new_size;
    }
    pos = s->string_table_len;
    memcpy(s->string_table + pos, name, name_size);
    s->string_table_len = new_len;
    return pos;
}

static void fdt_prop(FDTState *s, const char *prop_name,
                     const void *data, int data_len)
{
    fdt_put32(s, FDT_PROP);
    fdt_put32(s, data_len);
    fdt_put32(s, fdt_get_string_offset(s, prop_name));
    fdt_put_data(s, reinterpret_cast<const uint8_t*>(data), data_len);
}

static void fdt_prop_tab_u32(FDTState *s, const char *prop_name,
                             uint32_t *tab, int tab_len)
{
    int i;
    fdt_put32(s, FDT_PROP);
    fdt_put32(s, tab_len * sizeof(uint32_t));
    fdt_put32(s, fdt_get_string_offset(s, prop_name));
    for(i = 0; i < tab_len; i++)
        fdt_put32(s, tab[i]);
}

static void fdt_prop_u32(FDTState *s, const char *prop_name, uint32_t val)
{
    fdt_prop_tab_u32(s, prop_name, &val, 1);
}

static void fdt_prop_tab_u64(FDTState *s, const char *prop_name,
                             uint64_t v0)
{
    uint32_t tab[2];
    tab[0] = v0 >> 32;
    tab[1] = v0;
    fdt_prop_tab_u32(s, prop_name, tab, 2);
}

static void fdt_prop_tab_u64_2(FDTState *s, const char *prop_name,
                               uint64_t v0, uint64_t v1)
{
    uint32_t tab[4];
    tab[0] = v0 >> 32;
    tab[1] = v0;
    tab[2] = v1 >> 32;
    tab[3] = v1;
    fdt_prop_tab_u32(s, prop_name, tab, 4);
}

static void fdt_prop_str(FDTState *s, const char *prop_name,
                         const char *str)
{
    fdt_prop(s, prop_name, str, strlen(str) + 1);
}

/* NULL terminated string list */
static void fdt_prop_tab_str(FDTState *s, const char *prop_name,
                             ...)
{
    va_list ap;
    int size, str_size;
    char *ptr, *tab;

    va_start(ap, prop_name);
    size = 0;
    for(;;) {
        ptr = va_arg(ap, char *);
        if (!ptr)
            break;
        str_size = strlen(ptr) + 1;
        size += str_size;
    }
    va_end(ap);
    
    tab = reinterpret_cast<char*>(malloc(size));
    va_start(ap, prop_name);
    size = 0;
    for(;;) {
        ptr = va_arg(ap, char *);
        if (!ptr)
            break;
        str_size = strlen(ptr) + 1;
        memcpy(tab + size, ptr, str_size);
        size += str_size;
    }
    va_end(ap);
    
    fdt_prop(s, prop_name, tab, size);
    free(tab);
}

/* write the FDT to 'dst1'. return the FDT size in bytes */
int fdt_output(FDTState *s, uint8_t *dst)
{
    struct fdt_header *h;
    struct fdt_reserve_entry *re;
    int dt_struct_size;
    int dt_strings_size;
    int pos;

    assert(s->open_node_count == 0);
    
    fdt_put32(s, FDT_END);
    
    dt_struct_size = s->tab_len * sizeof(uint32_t);
    dt_strings_size = s->string_table_len;

    h = (struct fdt_header *)dst;
    h->magic = bswap<false>(FDT_MAGIC);
    h->version = bswap<false>(FDT_VERSION);
    h->last_comp_version = bswap<false>(16);
    h->boot_cpuid_phys = bswap<false>(0);
    h->size_dt_strings = bswap<false>(dt_strings_size);
    h->size_dt_struct = bswap<false>(dt_struct_size);

    pos = sizeof(struct fdt_header);

    h->off_dt_struct = bswap<false>(pos);
    memcpy(dst + pos, s->tab, dt_struct_size);
    pos += dt_struct_size;

    /* align to 8 */
    while ((pos & 7) != 0) {
        dst[pos++] = 0;
    }
    h->off_mem_rsvmap = bswap<false>(pos);
    re = (struct fdt_reserve_entry *)(dst + pos);
    re->address = 0; /* no reserved entry */
    re->size = 0;
    pos += sizeof(struct fdt_reserve_entry);

    h->off_dt_strings = bswap<false>(pos);
    memcpy(dst + pos, s->string_table, dt_strings_size);
    pos += dt_strings_size;

    /* align to 8, just in case */
    while ((pos & 7) != 0) {
        dst[pos++] = 0;
    }

    h->totalsize = bswap<false>(pos);
    return pos;
}

void fdt_end(FDTState *s)
{
    free(s->tab);
    free(s->string_table);
    free(s);
}

int riscv_build_fdt(uint8_t *dst,
		    uint64_t kernel_start, uint64_t kernel_size,
		    uint64_t initrd_start, uint64_t initrd_size,
		    const char *cmd_line)
{
    FDTState *s;
    int size, max_xlen = 64, i, cur_phandle, intc_phandle, plic_phandle;
    char isa_string[128], *q;
    uint32_t misa;
    uint32_t tab[4];
    uint64_t ram_size = (1UL<<32);

    
    s = fdt_init();

    cur_phandle = 1;
    
    fdt_begin_node(s, "");
    fdt_prop_u32(s, "#address-cells", 2);
    fdt_prop_u32(s, "#size-cells", 2);
    fdt_prop_str(s, "compatible", "ucbbar,riscvemu-bar_dev");
    fdt_prop_str(s, "model", "ucbbar,riscvemu-bare");

    /* CPU list */
    fdt_begin_node(s, "cpus");
    fdt_prop_u32(s, "#address-cells", 1);
    fdt_prop_u32(s, "#size-cells", 0);
    fdt_prop_u32(s, "timebase-frequency", RTC_FREQ);

    /* cpu */
    fdt_begin_node_num(s, "cpu", 0);
    fdt_prop_str(s, "device_type", "cpu");
    fdt_prop_u32(s, "reg", 0);
    fdt_prop_str(s, "status", "okay");
    fdt_prop_str(s, "compatible", "riscv");

    //misa = riscv_cpu_get_misa(m->cpu_state);
    /* blindly copied from tinyemu */
    misa = 0x141101;
    //misa &= (~4UL);
    //misa &= (~8UL);
    //misa &= (~32UL);
    //for(int i = 0; i < 26; i++) {
    //if(misa & (1<<i)) {
    //std::cout << "bit " << i << " set\n";
    //}
    //}
    //std::cout << std::hex << misa << std::dec << "\n";
    
    q = isa_string;
    q += snprintf(isa_string, sizeof(isa_string), "rv%d", max_xlen);
    for(i = 0; i < 26; i++) {
        if (misa & (1 << i))
            *q++ = 'a' + i;
    }
    *q = '\0';
    //std::cout << isa_string << "\n";
    fdt_prop_str(s, "riscv,isa", isa_string);
    
    fdt_prop_str(s, "mmu-type", max_xlen <= 32 ? "riscv,sv32" : "riscv,sv39");
    fdt_prop_u32(s, "clock-frequency", 2000000000);

    fdt_begin_node(s, "interrupt-controller");
    fdt_prop_u32(s, "#interrupt-cells", 1);
    fdt_prop(s, "interrupt-controller", NULL, 0);
    fdt_prop_str(s, "compatible", "riscv,cpu-intc");
    intc_phandle = cur_phandle++;
    fdt_prop_u32(s, "phandle", intc_phandle);
    fdt_end_node(s); /* interrupt-controller */
    
    fdt_end_node(s); /* cpu */
    
    fdt_end_node(s); /* cpus */

    fdt_begin_node_num(s, "memory", RAM_BASE_ADDR);
    fdt_prop_str(s, "device_type", "memory");
    tab[0] = (uint64_t)RAM_BASE_ADDR >> 32;
    tab[1] = RAM_BASE_ADDR;
    tab[2] = ram_size >> 32;
    tab[3] = ram_size;
    fdt_prop_tab_u32(s, "reg", tab, 4);
    
    fdt_end_node(s); /* memory */

    fdt_begin_node(s, "htif");
    fdt_prop_str(s, "compatible", "ucb,htif0");
    fdt_end_node(s); /* htif */

    fdt_begin_node(s, "soc");
    fdt_prop_u32(s, "#address-cells", 2);
    fdt_prop_u32(s, "#size-cells", 2);
    fdt_prop_tab_str(s, "compatible",
                     "ucbbar,riscvemu-bar-soc", "simple-bus", NULL);
    fdt_prop(s, "ranges", NULL, 0);

    fdt_begin_node_num(s, "clint", CLINT_BASE_ADDR);
    fdt_prop_str(s, "compatible", "riscv,clint0");

    tab[0] = intc_phandle;
    tab[1] = 3; /* M IPI irq */
    tab[2] = intc_phandle;
    tab[3] = 7; /* M timer irq */
    fdt_prop_tab_u32(s, "interrupts-extended", tab, 4);

    fdt_prop_tab_u64_2(s, "reg", CLINT_BASE_ADDR, CLINT_SIZE);
    
    fdt_end_node(s); /* clint */

    fdt_begin_node_num(s, "plic", PLIC_BASE_ADDR);
    fdt_prop_u32(s, "#interrupt-cells", 1);
    fdt_prop(s, "interrupt-controller", NULL, 0);
    fdt_prop_str(s, "compatible", "riscv,plic0");
    fdt_prop_u32(s, "riscv,ndev", 31);
    fdt_prop_tab_u64_2(s, "reg", PLIC_BASE_ADDR, PLIC_SIZE);

    tab[0] = intc_phandle;
    tab[1] = 9; /* S ext irq */
    tab[2] = intc_phandle;
    tab[3] = 11; /* M ext irq */
    fdt_prop_tab_u32(s, "interrupts-extended", tab, 4);

    plic_phandle = cur_phandle++;
    fdt_prop_u32(s, "phandle", plic_phandle);

    fdt_end_node(s); /* plic */

    for(i = 0; i < 1; i++) {
        fdt_begin_node_num(s, "virtio", VIRTIO_BASE_ADDR + i * VIRTIO_SIZE);
        fdt_prop_str(s, "compatible", "virtio,mmio");
        fdt_prop_tab_u64_2(s, "reg", VIRTIO_BASE_ADDR + i * VIRTIO_SIZE,
                           VIRTIO_SIZE);
        tab[0] = plic_phandle;
        tab[1] = VIRTIO_IRQ + i;
        fdt_prop_tab_u32(s, "interrupts-extended", tab, 2);
        fdt_end_node(s); /* virtio */
    }
    
#if 0
    FBDevice *fb_dev = m->common.fb_dev;
    if (fb_dev) {
        fdt_begin_node_num(s, "framebuffer", FRAMEBUFFER_BASE_ADDR);
        fdt_prop_str(s, "compatible", "simple-framebuffer");
        fdt_prop_tab_u64_2(s, "reg", FRAMEBUFFER_BASE_ADDR, fb_dev->fb_size);
        fdt_prop_u32(s, "width", fb_dev->width);
        fdt_prop_u32(s, "height", fb_dev->height);
        fdt_prop_u32(s, "stride", fb_dev->stride);
        fdt_prop_str(s, "format", "a8r8g8b8");
        fdt_end_node(s); /* framebuffer */
    }
#endif
    
    fdt_end_node(s); /* soc */

    fdt_begin_node(s, "chosen");
    fdt_prop_str(s, "bootargs", cmd_line ? cmd_line : "");
    if (kernel_size > 0) {
        fdt_prop_tab_u64(s, "riscv,kernel-start", kernel_start);
        fdt_prop_tab_u64(s, "riscv,kernel-end", kernel_start + kernel_size);
    }
    if (initrd_size > 0) {
        fdt_prop_tab_u64(s, "linux,initrd-start", initrd_start);
        fdt_prop_tab_u64(s, "linux,initrd-end", initrd_start + initrd_size);
    }
    

    fdt_end_node(s); /* chosen */
    
    fdt_end_node(s); /* / */

    size = fdt_output(s, dst);
#if 1
    {
        FILE *f;
        f = fopen("/tmp/riscvemu.dtb", "wb");
        fwrite(dst, 1, size, f);
        fclose(f);
    }
#endif
    fdt_end(s);
    return size;
}
