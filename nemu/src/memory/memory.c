#include "nemu.h"

#define CR0_PG    0x80000000  // Paging

#define PGSHFT    12      // log2(PGSIZE)
#define PTXSHFT   12      // Offset of PTX in a linear address
#define PDXSHFT   22      // Offset of PDX in a linear address

#define PTE_P     0x001     // Present

// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(va) --/ \--- PTX(va) --/\------ OFF(va) ------/
typedef uint32_t PTE;
typedef uint32_t PDE;
#define PDX(va)     (((uint32_t)(va) >> PDXSHFT) & 0x3ff)
#define PTX(va)     (((uint32_t)(va) >> PTXSHFT) & 0x3ff)
#define OFF(va)     ((uint32_t)(va) & 0xfff)

// Address in page table or page directory entry
#define PTE_ADDR(pte)   ((uint32_t)(pte) & ~0xfff)

#define PMEM_SIZE (128 * 1024 * 1024)
#define PGSIZE    4096    // Bytes mapped by a page

#define pmem_rw(addr, type) *(type *)({\
    Assert(addr < PMEM_SIZE, "physical address(0x%08x) is out of bound", addr); \
    guest_to_host(addr); \
    })

uint8_t pmem[PMEM_SIZE];
int is_mmio(paddr_t addr);
uint32_t mmio_read(paddr_t addr, int len, int mmio_id);
void mmio_write(paddr_t addr, int len, uint32_t data, int mmio_id);

/* Memory accessing interfaces */

uint32_t paddr_read(paddr_t addr, int len) {
  int mmio_id;
  if ((mmio_id = is_mmio(addr)) != -1) {
    return mmio_read(addr, len, mmio_id);
  }
  return pmem_rw(addr, uint32_t) & (~0u >> ((4 - len) << 3));
}

void paddr_write(paddr_t addr, uint32_t data, int len) {
  int mmio_id;
  if ((mmio_id = is_mmio(addr)) != -1) {
    mmio_write(addr, len, data, mmio_id);
    return;
  }
  memcpy(guest_to_host(addr), &data, len);
}

uint32_t vaddr_read(vaddr_t addr, int len) {
  if (cpu.CR0 & CR0_PG) {
    if (OFF(addr) + len > PGSIZE) {
      assert(0);
    }
    else {
      paddr_t paddr = page_translate(addr);
      return paddr_read(paddr, len);
    }
  }

  return paddr_read(addr, len);
}

void vaddr_write(vaddr_t addr, uint32_t data, int len) {
  if (cpu.CR0 & CR0_PG) {
    if (OFF(addr) + len > PGSIZE) {
      assert(0);
    }
    else {
      paddr_t paddr = page_translate(addr);
      paddr_write(paddr, data, len);
      return;
    }
  }

  paddr_write(addr, data, len);
}

paddr_t page_translate(vaddr_t vaddr) {
  PDE* pdir = (PDE*)(uintptr_t)PTE_ADDR(cpu.CR3);
  Log("PDX(vaddr)=%dpdir=0x%lx", PDX(vaddr), (uintptr_t)PTE_ADDR(cpu.CR3));
  assert(pdir[PDX(vaddr)] & PTE_P);
  PTE* ptab = (PTE*)(uintptr_t)PTE_ADDR(pdir[PDX(vaddr)]);
  assert(ptab[PTX(vaddr)] & PTE_P);
  return (PTE_ADDR(ptab[PTX(vaddr)]) + OFF(vaddr));
}