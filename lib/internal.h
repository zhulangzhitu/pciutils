/*
 *	$Id: internal.h,v 1.1 1999/01/22 21:05:29 mj Exp $
 *
 *	The PCI Library -- Internal Include File
 *
 *	Copyright (c) 1997--1999 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "pci.h"

#ifdef HAVE_PM_LINUX_BYTEORDER_H

#include <asm/byteorder.h>
#define cpu_to_le16 __cpu_to_le16
#define cpu_to_le32 __cpu_to_le32
#define le16_to_cpu __le16_to_cpu
#define le32_to_cpu __le32_to_cpu

#else

#include <endian.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#define cpu_to_le16 swab16
#define cpu_to_le32 swab32
#define le16_to_cpu swab16
#define le32_to_cpu swab32

static inline word swab16(word w)
{
  return (w << 8) | ((w >> 8) & 0xff);
}

static inline u32 swab32(u32 w)
{
  return ((w & 0xff000000) >> 24) |
         ((w & 0x00ff0000) >> 8) |
         ((w & 0x0000ff00) << 8)  |
         ((w & 0x000000ff) << 24);
}
#else
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#endif

#endif

struct pci_methods {
  char *name;
  void (*config)(struct pci_access *);
  int (*detect)(struct pci_access *);
  void (*init)(struct pci_access *);
  void (*cleanup)(struct pci_access *);
  void (*scan)(struct pci_access *);
  void (*fill_info)(struct pci_dev *, int flags);
  int (*read)(struct pci_dev *, int pos, byte *buf, int len);
  int (*write)(struct pci_dev *, int pos, byte *buf, int len);
  void (*init_dev)(struct pci_dev *);
  void (*cleanup_dev)(struct pci_dev *);
};

void pci_generic_scan(struct pci_access *);
void pci_generic_fill_info(struct pci_dev *, int flags);
int pci_generic_block_read(struct pci_dev *, int pos, byte *buf, int len);
int pci_generic_block_write(struct pci_dev *, int pos, byte *buf, int len);

void *pci_malloc(struct pci_access *, int);
void pci_mfree(void *);

struct pci_dev *pci_alloc_dev(struct pci_access *);
int pci_link_dev(struct pci_access *, struct pci_dev *);

extern struct pci_methods pm_intel_conf1, pm_intel_conf2, pm_linux_proc,
  pm_syscalls, pm_dump;

#define UNUSED __attribute__((unused))
