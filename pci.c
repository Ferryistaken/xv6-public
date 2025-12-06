#include "types.h"
#include "defs.h"
#include "x86.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

void
pci_print_irq(uint bus, uint slot)
{
  uint v = pci_config_read32(bus, slot, 0, 0x3C);
  uchar line = v & 0xFF;
  uchar pin  = (v >> 8) & 0xFF;
  cprintf("pci: bus %d slot %d: int line=%d pin=%d\n",
          bus, slot, line, pin);
}

uint
pci_config_read32(uint bus, uint slot, uint func, uint offset)
{
  uint address =
    (1U << 31) |
    (bus  << 16) |
    (slot << 11) |
    (func << 8) |
    (offset & 0xFC);

  outl(PCI_CONFIG_ADDRESS, address);
  return inl(PCI_CONFIG_DATA);
}

void
pci_config_write32(uint bus, uint slot, uint func, uint offset, uint val)
{
  uint address =
    (1U << 31) |
    (bus  << 16) |
    (slot << 11) |
    (func << 8)  |
    (offset & 0xFC);

  outl(PCI_CONFIG_ADDRESS, address);
  outl(PCI_CONFIG_DATA, val);
}

void
pci_scan_bus0(void)
{
  cprintf("Scanning for pci devices...\n");
  for(uint slot = 0; slot < 32; slot++) {
    uint v = pci_config_read32(0, slot, 0, 0);
    ushort vendor = v & 0xFFFF;
    ushort device = (v >> 16) & 0xFFFF;

    if(vendor == 0xFFFF)
      continue;

    cprintf("\tpci: bus 0, slot %d: vendor 0x%x device 0x%x\n",
            slot, vendor, device);
  }
  pci_print_irq(0, 3);
}

