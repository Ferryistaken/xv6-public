#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "e1000.h"
#include "mmu.h"
#include "traps.h"

struct e1000_tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
struct e1000_rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));

uchar tx_bufs[TX_RING_SIZE][PKT_BUF_SIZE] __attribute__((aligned(16)));
uchar rx_bufs[RX_RING_SIZE][PKT_BUF_SIZE] __attribute__((aligned(16)));

int tx_tail;  // software view of TDT
int rx_next;  // next RX descriptor to check

volatile uint *e1000_regs;

extern pde_t *kpgdir;

// Provided by Rust (smoltcp stack)
// e1000.c
extern void rust_net_init(void);
extern void rust_net_poll(void);
extern uint net_time_msec(void);

// Time source for Rust/smoltcp
extern uint ticks;  // from trap.c in xv6

typedef char assert_tx_desc_size[
  sizeof(struct e1000_tx_desc) == 16 ? 1 : -1
];

typedef char assert_rx_desc_size[
  sizeof(struct e1000_rx_desc) == 16 ? 1 : -1
];

static uint
e1000_read_reg(uint offset)
{
  return e1000_regs[offset/4];
}

static void
e1000_write_reg(uint offset, uint val)
{
  e1000_regs[offset/4] = val;
}

static void
e1000_test_send(void)
{
  uchar pkt[64];

  // Fake Ethernet frame: dst broadcast, src all 1s, ethertype 0x9000, payload 'RUST!'
  int i;
  for(i = 0; i < 6; i++) pkt[i] = 0xff;        // dst MAC: ff:ff:ff:ff:ff:ff
  for(i = 0; i < 6; i++) pkt[6+i] = 0x11;      // src MAC: 11:11:11:11:11:11
  pkt[12] = 0x90; pkt[13] = 0x00;             // ethertype (made up)

  const char *msg = "hello from xv6 e1000\n";
  int mlen = strlen(msg);
  if(mlen > (int)(sizeof(pkt) - 14))
    mlen = sizeof(pkt) - 14;
  memmove(&pkt[14], msg, mlen);

  int len = 14 + mlen;
  if(len < 60) len = 60; // minimum Ethernet frame size (without FCS)

  if(e1000_tx(pkt, len) == 0)
    cprintf("e1000: test packet sent (%d bytes)\n", len);
  else
    cprintf("e1000: TX ring full, test packet not sent\n");
}

void
e1000_debug_dump(void)
{
  cprintf("e1000_debug_dump: TDH=%d TDT=%d\n",
          e1000_read_reg(E1000_TDH),
          e1000_read_reg(E1000_TDT));

  for (int i = 0; i < 4; i++) {
    cprintf("  tx[%d]: addr_lo=0x%x len=%d cmd=0x%x status=0x%x\n",
            i, tx_ring[i].addr_lo, tx_ring[i].length,
            tx_ring[i].cmd, tx_ring[i].status);
  }
}

void
e1000_init(void)
{
  ioapicenable(IRQ_E1000, 0);
  uint bus = 0;
  uint slot = 3;

  uint bar0 = pci_config_read32(bus, slot, 0, 0x10);
  uint mmio_pa = bar0 & ~0xF;

  uint pa_page = mmio_pa & ~(PGSIZE - 1);
  void *va_page = (void*)P2V(pa_page);
  uint mmio_size = 0x4000; // 4 pages

  int perm = PTE_W | PTE_P;

  if(mappages(kpgdir, va_page, mmio_size, pa_page, perm) < 0){
    cprintf("e1000: mappages failed for mmio_pa=0x%x\n", mmio_pa);
    return;
  }

  // Reload CR3 so CPU picks up new mapping
  lcr3(V2P(kpgdir));

  e1000_regs = (volatile uint *)P2V(mmio_pa);
  cprintf("e1000: BAR0=0x%x mmio_pa=0x%x regs=%p\n", bar0, mmio_pa, e1000_regs);

  uint status = e1000_read_reg(E1000_STATUS);
  cprintf("e1000: status=0x%x\n", status);

  // Enable bus mastering + memory space on the PCI device
  uint cmd = pci_config_read32(bus, slot, 0, 0x04);
  ushort cmd_lo = cmd & 0xFFFF;

  // Bit 1 = Memory Space Enable, Bit 2 = Bus Master Enable
  cmd_lo |= 0x0002; // MSE
  cmd_lo |= 0x0004; // BME

  cmd = (cmd & 0xFFFF0000) | cmd_lo;
  pci_config_write32(bus, slot, 0, 0x04, cmd);

  cprintf("e1000: PCI CMD=0x%x\n", cmd_lo);

  // Init TX ring
  int i;
  for(i = 0; i < TX_RING_SIZE; i++){
    tx_ring[i].addr_lo = V2P(tx_bufs[i]);
    tx_ring[i].addr_hi = 0;
    tx_ring[i].length  = 0;
    tx_ring[i].cso     = 0;
    tx_ring[i].cmd     = E1000_TXD_CMD_RS | E1000_TXD_CMD_IFCS;
    tx_ring[i].status  = E1000_TXD_STAT_DD;  // mark free
    tx_ring[i].css     = 0;
    tx_ring[i].special = 0;
  }

  e1000_write_reg(E1000_TDBAL, V2P(tx_ring));
  e1000_write_reg(E1000_TDBAH, 0);
  e1000_write_reg(E1000_TDLEN, sizeof(tx_ring));
  e1000_write_reg(E1000_TDH, 0);
  e1000_write_reg(E1000_TDT, 0);
  tx_tail = 0;

  // Init RX ring
  for(i = 0; i < RX_RING_SIZE; i++){
    rx_ring[i].addr_lo = V2P(rx_bufs[i]);
    rx_ring[i].addr_hi = 0;
    rx_ring[i].length  = 0;
    rx_ring[i].csum    = 0;
    rx_ring[i].status  = 0;   // owned by NIC
    rx_ring[i].errors  = 0;
    rx_ring[i].special = 0;
  }

  e1000_write_reg(E1000_RDBAL, V2P(rx_ring));
  e1000_write_reg(E1000_RDBAH, 0);
  e1000_write_reg(E1000_RDLEN, sizeof(rx_ring));
  e1000_write_reg(E1000_RDH, 0);
  e1000_write_reg(E1000_RDT, RX_RING_SIZE - 1);
  rx_next = 0;

  // 5) Configure transmit control
  uint tctl = 0;
  tctl |= E1000_TCTL_EN;   // enable TX
  tctl |= E1000_TCTL_PSP;  // pad short packets
  tctl |= (0x10 << E1000_TCTL_CT_SHIFT);   // collision threshold ~16
  tctl |= (0x40 << E1000_TCTL_COLD_SHIFT); // collision distance ~64
  e1000_write_reg(E1000_TCTL, tctl);

  // 6) Configure inter-packet gap
  uint tipg = 0;
  tipg |= E1000_TIPG_IPGT;
  tipg |= (E1000_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT);
  tipg |= (E1000_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT);
  e1000_write_reg(E1000_TIPG, tipg);

  // 7) Configure receive control
  uint rctl = 0;
  rctl |= E1000_RCTL_EN;        // enable RX
  rctl |= E1000_RCTL_BAM;       // accept broadcast
  rctl |= E1000_RCTL_UPE;       // accept all unicast
  rctl |= E1000_RCTL_MPE;       // accept all multicast
  rctl |= E1000_RCTL_SZ_2048;   // 2K buffers
  rctl |= E1000_RCTL_SECRC;     // strip CRC
  e1000_write_reg(E1000_RCTL, rctl);

  e1000_write_reg(E1000_IMC, 0xffffffff);  // mask all
  (void)e1000_read_reg(E1000_ICR);         // clear pending
  e1000_write_reg(E1000_IMS,
                  E1000_IMS_RXT0 |   // RX timer / normal receive
                  E1000_IMS_RXO  |   // RX overrun (optional)
                  E1000_IMS_RXDMT0 | // RX desc low watermark
                  E1000_IMS_TXDW);   // TX descriptor write-back

  // debug: read back RCTL to confirm
  uint rctl_read = e1000_read_reg(E1000_RCTL);
  cprintf("e1000: RCTL=0x%x\n", rctl_read);

  (void)e1000_test_send;
  // e1000_test_send();  // optional

  cprintf("e1000: initialized\n");

  // Initialize Rust+smoltcp stack now that NIC is ready.
  rust_net_init();
}

int
e1000_tx(const void *data, int len)
{
  if(len <= 0 || len > PKT_BUF_SIZE)
    return -1;

  int tdt = e1000_read_reg(E1000_TDT);
  struct e1000_tx_desc *d = &tx_ring[tdt];

  cprintf("e1000_tx: tdt=%d status=0x%x\n", tdt, d->status);

  if((d->status & E1000_TXD_STAT_DD) == 0){
    cprintf("e1000_tx: ring full at %d\n", tdt);
    return -1;
  }

  memmove(tx_bufs[tdt], data, len);

  d->length = len;
  d->status = 0;
  d->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS;

  tdt = (tdt + 1) % TX_RING_SIZE;
  e1000_write_reg(E1000_TDT, tdt);

  cprintf("e1000_tx: queued len=%d new TDT=%d\n", len, tdt);
  return 0;
}

int
e1000_rx_poll(uchar *buf, int maxlen)
{
  struct e1000_rx_desc *d = &rx_ring[rx_next];

  if((d->status & E1000_RXD_STAT_DD) == 0)
    return -1;

  int n = 0;

  if((d->status & E1000_RXD_STAT_EOP) == 0){
    cprintf("e1000: RX fragment, dropping (len=%d status=0x%x)\n",
                  d->length, d->status);
    // n stays 0 -> caller won't use this packet
    goto rearm;
  }

  n = d->length;
  if(n > maxlen)
    n = maxlen;

  memmove(buf, rx_bufs[rx_next], n);

rearm:
  d->status = 0;
  d->errors = 0;

  rx_next = (rx_next + 1) % RX_RING_SIZE;
  int rdt = (rx_next + RX_RING_SIZE - 1) % RX_RING_SIZE;
  e1000_write_reg(E1000_RDT, rdt);

  return n;
}

void
e1000_intr(void)
{
  uint icr = e1000_read_reg(E1000_ICR);

  if(icr == 0)
    return;

  // RX-related interrupt?
  if(icr & (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO)){
    // Let Rust+smoltcp pull packets with e1000_rx_poll() and handle them.
    cprintf("handling packet in rust\n");
    rust_net_poll();
  }

  if(icr & E1000_ICR_TXDW){
    cprintf("e1000: TXDW\n");
  }

  (void)e1000_debug_dump;
}

// Monotonic time in milliseconds for smoltcp
uint
net_time_msec(void)
{
  // xv6 timer runs at 100 Hz: 1 tick = 10ms
  // adjust factor if you've changed HZ.
  return (uint)ticks * 10ULL;
}
