/*
 *	The PCI Utilities -- Manipulate PCI Configuration Registers
 *
 *	Copyright (c) 1998--2008 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#define PCIUTILS_SETPCI
#include "pciutils.h"

static int force;			/* Don't complain if no devices match */
static int verbose;			/* Verbosity level */
static int demo_mode;			/* Only show */

const char program_name[] = "setpci";

static struct pci_access *pacc;

struct value {
  unsigned int value;
  unsigned int mask;
};

struct op {
  struct op *next;
  struct pci_dev **dev_vector;
  unsigned int cap;			/* Capability: 0=none, 10000-100ff=normal, 20000-2ffff=extended */
  unsigned int addr;
  unsigned int width;			/* Byte width of the access */
  unsigned int num_values;		/* Number of values to write; 0=read */
  struct value values[0];
};

static struct op *first_op, **last_op = &first_op;
static unsigned int max_values[] = { 0, 0xff, 0xffff, 0, 0xffffffff };

static struct pci_dev **
select_devices(struct pci_filter *filt)
{
  struct pci_dev *z, **a, **b;
  int cnt = 1;

  for (z=pacc->devices; z; z=z->next)
    if (pci_filter_match(filt, z))
      cnt++;
  a = b = xmalloc(sizeof(struct device *) * cnt);
  for (z=pacc->devices; z; z=z->next)
    if (pci_filter_match(filt, z))
      *a++ = z;
  *a = NULL;
  return b;
}

static void
exec_op(struct op *op, struct pci_dev *dev)
{
  char *formats[] = { NULL, "%02x", "%04x", NULL, "%08x" };
  char *mask_formats[] = { NULL, "%02x->(%02x:%02x)->%02x", "%04x->(%04x:%04x)->%04x", NULL, "%08x->(%08x:%08x)->%08x" };
  unsigned int i, x, y;
  int addr = 0;
  int width = op->width;

  if (verbose)
    printf("%02x:%02x.%x", dev->bus, dev->dev, dev->func);
  if (op->cap)
    {
      struct pci_cap *cap;
      if (op->cap < 0x20000)
	{
	  if (verbose)
	    printf("(cap %02x)", op->cap - 0x10000);
	  cap = pci_find_cap(dev, op->cap - 0x10000, PCI_CAP_NORMAL);
	}
      else
	{
	  if (verbose)
	    printf("(ecap %04x)", op->cap - 0x20000);
	  cap = pci_find_cap(dev, op->cap - 0x20000, PCI_CAP_EXTENDED);
	}
      if (cap)
	addr = cap->addr;
      else
	{
	  /* FIXME: Report the error properly */
	  die("Capability %08x not found", op->cap);
	}
    }
  addr += op->addr;
  if (verbose)
    printf(":%02x", addr);
  if (op->num_values)
    {
      for (i=0; i<op->num_values; i++)
	{
	  if (addr + width > 0x1000)
	    die("Out of range");	/* FIXME */
	  if ((op->values[i].mask & max_values[width]) == max_values[width])
	    {
	      x = op->values[i].value;
	      if (verbose)
		{
		  putchar(' ');
		  printf(formats[width], op->values[i].value);
		}
	    }
	  else
	    {
	      switch (width)
		{
		case 1:
		  y = pci_read_byte(dev, addr);
		  break;
		case 2:
		  y = pci_read_word(dev, addr);
		  break;
		default:
		  y = pci_read_long(dev, addr);
		  break;
		}
	      x = (y & ~op->values[i].mask) | op->values[i].value;
	      if (verbose)
		{
		  putchar(' ');
		  printf(mask_formats[width], y, op->values[i].value, op->values[i].mask, x);
		}
	    }
	  if (!demo_mode)
	    {
	      switch (width)
		{
		case 1:
		  pci_write_byte(dev, addr, x);
		  break;
		case 2:
		  pci_write_word(dev, addr, x);
		  break;
		default:
		  pci_write_long(dev, addr, x);
		  break;
		}
	    }
	  addr += width;
	}
      if (verbose)
	putchar('\n');
    }
  else
    {
      if (verbose)
	printf(" = ");
      if (addr + width > 0x1000)
	die("Out of range");	/* FIXME */
      switch (width)
	{
	case 1:
	  x = pci_read_byte(dev, addr);
	  break;
	case 2:
	  x = pci_read_word(dev, addr);
	  break;
	default:
	  x = pci_read_long(dev, addr);
	  break;
	}
      printf(formats[width], x);
      putchar('\n');
    }
}

static void
execute(struct op *op)
{
  struct pci_dev **vec = NULL;
  struct pci_dev **pdev, *dev;
  struct op *oops;

  while (op)
    {
      pdev = vec = op->dev_vector;
      while (dev = *pdev++)
	for (oops=op; oops && oops->dev_vector == vec; oops=oops->next)
	  exec_op(oops, dev);
      while (op && op->dev_vector == vec)
	op = op->next;
    }
}

static void
scan_ops(struct op *op)
{
  while (op)
    {
      if (op->num_values)
	pacc->writeable = 1;
      op = op->next;
    }
}

struct reg_name {
  unsigned int cap;
  unsigned int offset;
  unsigned int width;
  const char *name;
};

static const struct reg_name pci_reg_names[] = {
  {       0, 0x00, 2, "VENDOR_ID" },
  {       0, 0x02, 2, "DEVICE_ID" },
  {       0, 0x04, 2, "COMMAND" },
  {       0, 0x06, 2, "STATUS" },
  {       0, 0x08, 1, "REVISION" },
  {       0, 0x09, 1, "CLASS_PROG" },
  {       0, 0x0a, 2, "CLASS_DEVICE" },
  {       0, 0x0c, 1, "CACHE_LINE_SIZE" },
  {       0, 0x0d, 1, "LATENCY_TIMER" },
  {       0, 0x0e, 1, "HEADER_TYPE" },
  {       0, 0x0f, 1, "BIST" },
  {       0, 0x10, 4, "BASE_ADDRESS_0" },
  {       0, 0x14, 4, "BASE_ADDRESS_1" },
  {       0, 0x18, 4, "BASE_ADDRESS_2" },
  {       0, 0x1c, 4, "BASE_ADDRESS_3" },
  {       0, 0x20, 4, "BASE_ADDRESS_4" },
  {       0, 0x24, 4, "BASE_ADDRESS_5" },
  {       0, 0x28, 4, "CARDBUS_CIS" },
  {       0, 0x2c, 4, "SUBSYSTEM_VENDOR_ID" },
  {       0, 0x2e, 2, "SUBSYSTEM_ID" },
  {       0, 0x30, 4, "ROM_ADDRESS" },
  {       0, 0x3c, 1, "INTERRUPT_LINE" },
  {       0, 0x3d, 1, "INTERRUPT_PIN" },
  {       0, 0x3e, 1, "MIN_GNT" },
  {       0, 0x3f, 1, "MAX_LAT" },
  {       0, 0x18, 1, "PRIMARY_BUS" },
  {       0, 0x19, 1, "SECONDARY_BUS" },
  {       0, 0x1a, 1, "SUBORDINATE_BUS" },
  {       0, 0x1b, 1, "SEC_LATENCY_TIMER" },
  {       0, 0x1c, 1, "IO_BASE" },
  {       0, 0x1d, 1, "IO_LIMIT" },
  {       0, 0x1e, 2, "SEC_STATUS" },
  {       0, 0x20, 2, "MEMORY_BASE" },
  {       0, 0x22, 2, "MEMORY_LIMIT" },
  {       0, 0x24, 2, "PREF_MEMORY_BASE" },
  {       0, 0x26, 2, "PREF_MEMORY_LIMIT" },
  {       0, 0x28, 4, "PREF_BASE_UPPER32" },
  {       0, 0x2c, 4, "PREF_LIMIT_UPPER32" },
  {       0, 0x30, 2, "IO_BASE_UPPER16" },
  {       0, 0x32, 2, "IO_LIMIT_UPPER16" },
  {       0, 0x38, 4, "BRIDGE_ROM_ADDRESS" },
  {       0, 0x3e, 2, "BRIDGE_CONTROL" },
  {       0, 0x10, 4, "CB_CARDBUS_BASE" },
  {       0, 0x14, 2, "CB_CAPABILITIES" },
  {       0, 0x16, 2, "CB_SEC_STATUS" },
  {       0, 0x18, 1, "CB_BUS_NUMBER" },
  {       0, 0x19, 1, "CB_CARDBUS_NUMBER" },
  {       0, 0x1a, 1, "CB_SUBORDINATE_BUS" },
  {       0, 0x1b, 1, "CB_CARDBUS_LATENCY" },
  {       0, 0x1c, 4, "CB_MEMORY_BASE_0" },
  {       0, 0x20, 4, "CB_MEMORY_LIMIT_0" },
  {       0, 0x24, 4, "CB_MEMORY_BASE_1" },
  {       0, 0x28, 4, "CB_MEMORY_LIMIT_1" },
  {       0, 0x2c, 2, "CB_IO_BASE_0" },
  {       0, 0x2e, 2, "CB_IO_BASE_0_HI" },
  {       0, 0x30, 2, "CB_IO_LIMIT_0" },
  {       0, 0x32, 2, "CB_IO_LIMIT_0_HI" },
  {       0, 0x34, 2, "CB_IO_BASE_1" },
  {       0, 0x36, 2, "CB_IO_BASE_1_HI" },
  {       0, 0x38, 2, "CB_IO_LIMIT_1" },
  {       0, 0x3a, 2, "CB_IO_LIMIT_1_HI" },
  {       0, 0x40, 2, "CB_SUBSYSTEM_VENDOR_ID" },
  {       0, 0x42, 2, "CB_SUBSYSTEM_ID" },
  {       0, 0x44, 4, "CB_LEGACY_MODE_BASE" },
  { 0x10001,    0, 0, "CAP_PM" },
  { 0x10002,    0, 0, "CAP_AGP" },
  { 0x10003,    0, 0, "CAP_VPD" },
  { 0x10004,    0, 0, "CAP_SLOTID" },
  { 0x10005,    0, 0, "CAP_MSI" },
  { 0x10006,    0, 0, "CAP_CHSWP" },
  { 0x10007,    0, 0, "CAP_PCIX" },
  { 0x10008,    0, 0, "CAP_HT" },
  { 0x10009,    0, 0, "CAP_VNDR" },
  { 0x1000a,    0, 0, "CAP_DBG" },
  { 0x1000b,    0, 0, "CAP_CCRC" },
  { 0x1000c,    0, 0, "CAP_HOTPLUG" },
  { 0x1000d,    0, 0, "CAP_SSVID" },
  { 0x1000e,    0, 0, "CAP_AGP3" },
  { 0x1000f,    0, 0, "CAP_SECURE" },
  { 0x10010,    0, 0, "CAP_EXP" },
  { 0x10011,    0, 0, "CAP_MSIX" },
  { 0x10012,    0, 0, "CAP_SATA" },
  { 0x10013,    0, 0, "CAP_AF" },
  { 0x20001,	0, 0, "ECAP_AER" },
  { 0x20002,	0, 0, "ECAP_VC" },
  { 0x20003,	0, 0, "ECAP_DSN" },
  { 0x20004,	0, 0, "ECAP_PB" },
  { 0x20005,	0, 0, "ECAP_RCLINK" },
  { 0x20006,	0, 0, "ECAP_RCILINK" },
  { 0x20007,	0, 0, "ECAP_RCECOLL" },
  { 0x20008,	0, 0, "ECAP_MFVC" },
  { 0x2000a,	0, 0, "ECAP_RBCB" },
  { 0x2000b,	0, 0, "ECAP_VNDR" },
  { 0x2000d,	0, 0, "ECAP_ACS" },
  { 0x2000e,	0, 0, "ECAP_ARI" },
  { 0x2000f,	0, 0, "ECAP_ATS" },
  { 0x20010,	0, 0, "ECAP_SRIOV" },
  {       0,    0, 0, NULL }
};

static void NONRET PCI_PRINTF(1,2)
usage(char *msg, ...)
{
  va_list args;
  va_start(args, msg);
  if (msg)
    {
      fprintf(stderr, "setpci: ");
      vfprintf(stderr, msg, args);
      fprintf(stderr, "\n\n");
    }
  fprintf(stderr,
"Usage: setpci [<options>] (<device>+ <reg>[=<values>]*)*\n"
"\n"
"General options:\n"
"-f\t\tDon't complain if there's nothing to do\n"
"-v\t\tBe verbose\n"
"-D\t\tList changes, don't commit them\n"
"\n"
"PCI access options:\n"
GENERIC_HELP
"\n"
"Setting commands:\n"
"<device>:\t-s [[[<domain>]:][<bus>]:][<slot>][.[<func>]]\n"
"\t\t-d [<vendor>]:[<device>]\n"
"<reg>:\t\t<base>[+<offset>][.(B|W|L)]\n"
"<base>:\t\t<address>\n"
"\t\t<named-register>\n"
"\t\t[E]CAP_<capability-name>\n"
"\t\t[E]CAP<capability-number>\n"
"<values>:\t<value>[,<value>...]\n"
"<value>:\t<hex>\n"
"\t\t<hex>:<mask>\n");
  exit(1);
}

static int
parse_options(int argc, char **argv)
{
  char *opts = GENERIC_OPTIONS;
  int i=1;

  if (argc == 2 && !strcmp(argv[1], "--version"))
    {
      puts("setpci version " PCIUTILS_VERSION);
      exit(0);
    }

  while (i < argc && argv[i][0] == '-')
    {
      char *c = argv[i++] + 1;
      char *d = c;
      char *e;
      while (*c)
	switch (*c)
	  {
	  case 0:
	    break;
	  case 'v':
	    verbose++;
	    c++;
	    break;
	  case 'f':
	    force++;
	    c++;
	    break;
	  case 'D':
	    demo_mode++;
	    c++;
	    break;
	  default:
	    if (e = strchr(opts, *c))
	      {
		char *arg;
		c++;
		if (e[1] == ':')
		  {
		    if (*c)
		      arg = c;
		    else if (i < argc)
		      arg = argv[i++];
		    else
		      usage(NULL);
		    c = "";
		  }
		else
		  arg = NULL;
		if (!parse_generic_option(*e, pacc, arg))
		  usage(NULL);
	      }
	    else
	      {
		if (c != d)
		  usage(NULL);
		return i-1;
	      }
	  }
    }

  return i;
}

static int parse_filter(int argc, char **argv, int i, struct pci_filter *filter)
{
  char *c = argv[i++];
  char *d;

  if (!c[1] || !strchr("sd", c[1]))
    usage(NULL);
  if (c[2])
    d = (c[2] == '=') ? c+3 : c+2;
  else if (i < argc)
    d = argv[i++];
  else
    usage(NULL);
  switch (c[1])
    {
    case 's':
      if (d = pci_filter_parse_slot(filter, d))
	die("-s: %s", d);
      break;
    case 'd':
      if (d = pci_filter_parse_id(filter, d))
	die("-d: %s", d);
      break;
    default:
      usage(NULL);
    }

  return i;
}

static const struct reg_name *parse_reg_name(char *name)
{
  const struct reg_name *r;

  for (r = pci_reg_names; r->name; r++)
    if (!strcasecmp(r->name, name))
      return r;
  return NULL;
}

static int parse_x32(char *c, char **stopp, unsigned int *resp)
{
  char *stop;

  if (!*c)
    return -1;
  errno = 0;
  unsigned long int l = strtoul(c, &stop, 16);
  if (errno)
    return -1;
  if ((l & ~0U) != l)
    return -1;
  *resp = l;
  if (*stop)
    {
      if (stopp)
	*stopp = stop;
      return 0;
    }
  else
    return 1;
}

static void parse_register(struct op *op, char *base)
{
  const struct reg_name *r;

  if (parse_x32(base, NULL, &op->addr) > 0)
    {
      op->cap = 0;
      return;
    }
  else if (r = parse_reg_name(base))
    {
      op->cap = r->cap;
      op->addr = r->offset;
      if (r->width && !op->width)
	op->width = r->width;
      return;
    }
  else if (!strncasecmp(base, "CAP", 3))
    {
      if (parse_x32(base+3, NULL, &op->cap) > 0 && op->cap < 0x100)
	{
	  op->cap += 0x10000;
	  op->addr = 0;
	  return;
	}
    }
  else if (!strncasecmp(base, "ECAP", 4))
    {
      if (parse_x32(base+4, NULL, &op->cap) > 0 && op->cap < 0x1000)
	{
	  op->cap += 0x20000;
	  op->addr = 0;
	  return;
	}
    }
  usage("Unknown register \"%s\"", base);
}

static void parse_op(char *c, struct pci_dev **selected_devices)
{
  char *base, *offset, *width, *value;
  char *e, *f;
  int n, j;
  struct op *op;

  /* Split the argument */
  base = xstrdup(c);
  if (value = strchr(base, '='))
    *value++ = 0;
  if (width = strchr(base, '.'))
    *width++ = 0;
  if (offset = strchr(base, '+'))
    *offset++ = 0;

  /* Look for setting of values and count how many */
  n = 0;
  if (value)
    {
      if (!*value)
	usage("Missing value");
      n++;
      for (e=value; *e; e++)
	if (*e == ',')
	  n++;
    }

  /* Allocate the operation */
  op = xmalloc(sizeof(struct op) + n*sizeof(struct value));
  op->dev_vector = selected_devices;
  op->num_values = n;

  /* What is the width suffix? */
  if (width)
    {
      if (width[1])
	usage("Invalid width \"%s\"", width);
      switch (*width & 0xdf)
	{
	case 'B':
	  op->width = 1; break;
	case 'W':
	  op->width = 2; break;
	case 'L':
	  op->width = 4; break;
	default:
	  usage("Invalid width \"%c\"", *width);
	}
    }
  else
    op->width = 0;

  /* Find the register */
  parse_register(op, base);
  if (!op->width)
    usage("Missing width");

  /* Add offset */
  if (offset)
    {
      unsigned int off;
      if (parse_x32(offset, NULL, &off) <= 0 || off >= 0x1000)
	die("Invalid offset \"%s\"", offset);
      op->addr += off;
    }

  /* Check range */
  if (op->addr >= 0x1000 || op->addr + op->width*(n ? n : 1) > 0x1000)
    die("Register number out of range!");
  if (op->addr & (op->width - 1))
    die("Unaligned register address!");

  /* Parse the values */
  for (j=0; j<n; j++)
    {
      unsigned int ll, lim;
      e = strchr(value, ',');
      if (e)
	*e++ = 0;
      if (parse_x32(value, &f, &ll) < 0 || *f && *f != ':')
	usage("Invalid value \"%s\"", value);
      lim = max_values[op->width];
      if (ll > lim && ll < ~0UL - lim)
	usage("Value \"%s\" is out of range", value);
      op->values[j].value = ll;
      if (*f == ':')
	{
	  if (parse_x32(f+1, NULL, &ll) <= 0)
	    usage("Invalid mask \"%s\"", f+1);
	  if (ll > lim && ll < ~0UL - lim)
	    usage("Mask \"%s\" is out of range", f+1);
	  op->values[j].mask = ll;
	  op->values[j].value &= ll;
	}
      else
	op->values[j].mask = ~0U;
      value = e;
    }

  *last_op = op;
  last_op = &op->next;
  op->next = NULL;
}

static void parse_ops(int argc, char **argv, int i)
{
  enum { STATE_INIT, STATE_GOT_FILTER, STATE_GOT_OP } state = STATE_INIT;
  struct pci_filter filter;
  struct pci_dev **selected_devices = NULL;

  while (i < argc)
    {
      char *c = argv[i++];

      if (*c == '-')
	{
	  if (state != STATE_GOT_FILTER)
	    pci_filter_init(pacc, &filter);
	  i = parse_filter(argc, argv, i-1, &filter);
	  state = STATE_GOT_FILTER;
	}
      else
	{
	  if (state == STATE_INIT)
	    usage(NULL);
	  if (state == STATE_GOT_FILTER)
	    selected_devices = select_devices(&filter);
	  if (!selected_devices[0] && !force)
	    fprintf(stderr, "setpci: Warning: No devices selected for `%s'.\n", c);
	  parse_op(c, selected_devices);
	  state = STATE_GOT_OP;
	}
    }
  if (state == STATE_INIT)
    usage("No operation specified");
}

int
main(int argc, char **argv)
{
  int i;

  pacc = pci_alloc();
  pacc->error = die;
  i = parse_options(argc, argv);

  pci_init(pacc);
  pci_scan_bus(pacc);

  parse_ops(argc, argv, i);
  scan_ops(first_op);
  execute(first_op);

  return 0;
}
