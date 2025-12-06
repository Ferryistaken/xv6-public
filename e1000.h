#include "types.h"

extern void e1000_intr(void);

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
// Interrupt registers
#define E1000_ICR    0xC0   // Interrupt Cause Read
#define E1000_IMS    0xD0   // Interrupt Mask Set
#define E1000_IMC    0xD8   // Interrupt Mask Clear

// ICR/IMS/IMC bit masks (we’ll use a subset)
#define E1000_ICR_TXDW    0x00000001   // TX descriptor written back
#define E1000_ICR_RXDMT0  0x00000010   // RX ring below threshold
#define E1000_ICR_RXO     0x00000040   // RX overrun
#define E1000_ICR_RXT0    0x00000080   // RX "timer" (normal receive)

// For convenience, mirror these for IMS/IMC
#define E1000_IMS_TXDW    E1000_ICR_TXDW
#define E1000_IMS_RXDMT0  E1000_ICR_RXDMT0
#define E1000_IMS_RXO     E1000_ICR_RXO
#define E1000_IMS_RXT0    E1000_ICR_RXT0

#define TX_RING_SIZE   16
#define RX_RING_SIZE   16
#define PKT_BUF_SIZE   2048   // fits in one 2k buffer

// MMIO register offsets (byte offsets, divide by 4 when indexing e1000_regs)
#define E1000_CTRL    0x0000
#define E1000_STATUS  0x0008

// RX
#define E1000_RCTL    0x0100
#define E1000_RDBAL   0x2800
#define E1000_RDBAH   0x2804
#define E1000_RDLEN   0x2808
#define E1000_RDH     0x2810
#define E1000_RDT     0x2818

#define E1000_RCTL_EN      0x00000002
#define E1000_RCTL_SBP     0x00000004
#define E1000_RCTL_UPE     0x00000008   // unicast promiscuous
#define E1000_RCTL_MPE     0x00000010   // multicast promiscuous
#define E1000_RCTL_BAM     0x00008000   // broadcast accept
#define E1000_RCTL_SECRC   0x04000000
#define E1000_RCTL_SZ_2048 0x00000000   // (on real E1000, 0 = 2K)

// TX
#define E1000_TCTL    0x0400
#define E1000_TIPG    0x0410
#define E1000_TDBAL   0x3800
#define E1000_TDBAH   0x3804
#define E1000_TDLEN   0x3808
#define E1000_TDH     0x3810
#define E1000_TDT     0x3818

// Transmit Control bits
#define E1000_TCTL_EN       0x00000002  // enable tx
#define E1000_TCTL_PSP      0x00000008  // pad short packets
#define E1000_TCTL_CT_SHIFT 4           // collision threshold field shift
#define E1000_TCTL_COLD_SHIFT 12        // collision distance field shift

// Receive Control bits
#define E1000_RCTL_EN       0x00000002  // enable rx
#define E1000_RCTL_BAM      0x00008000  // accept broadcast
#define E1000_RCTL_SZ_2048  0x00000000  // 2048-byte buffers
#define E1000_RCTL_SECRC    0x04000000  // strip Ethernet CRC

// TIPG fields (we'll use IPGT=10, IPGR1=8, IPGR2=6)
#define E1000_TIPG_IPGT        10
#define E1000_TIPG_IPGR1       8
#define E1000_TIPG_IPGR2       6
#define E1000_TIPG_IPGR1_SHIFT 10
#define E1000_TIPG_IPGR2_SHIFT 20

// TX descriptor cmd bits
#define E1000_TXD_CMD_EOP  0x01  // end of packet
#define E1000_TXD_CMD_IFCS 0x02  // insert FCS
#define E1000_TXD_CMD_RS   0x08  // report status

// TX descriptor status bits
#define E1000_TXD_STAT_DD  0x01  // descriptor done

// RX descriptor status bits
#define E1000_RXD_STAT_DD  0x01  // descriptor done
#define E1000_RXD_STAT_EOP 0x02  // end of packet


// Legacy TX descriptor (16 bytes)
struct e1000_tx_desc {
  uint   addr_lo;
  uint   addr_hi;
  ushort length;
  uchar  cso;
  uchar  cmd;
  uchar  status;
  uchar  css;
  ushort special;
} __attribute__((packed));

// Legacy RX descriptor (16 bytes)
struct e1000_rx_desc {
  uint   addr_lo;
  uint   addr_hi;
  ushort length;
  ushort csum;
  uchar  status;
  uchar  errors;
  ushort special;
} __attribute__((packed));

extern struct e1000_tx_desc tx_ring[TX_RING_SIZE];
extern struct e1000_rx_desc rx_ring[RX_RING_SIZE];

extern uchar tx_bufs[TX_RING_SIZE][PKT_BUF_SIZE];
extern uchar rx_bufs[RX_RING_SIZE][PKT_BUF_SIZE];

extern int tx_tail;
extern int rx_next;

extern volatile uint *e1000_regs;
