// SPDX-License-Identifier: GPL-2.0+

#include <linux/io.h>
#include "ipmi_si.h"

#ifdef CONFIG_ARM_GIC_PHYTIUM_2500
#include <linux/arm-smccc.h>

#define CTL_RST_FUNC_ID 0xC2000011

static bool apply_phytium2500_workaround;

struct ipmi_workaround_oem_info {
	char oem_id[ACPI_OEM_ID_SIZE + 1];
};

static struct ipmi_workaround_oem_info wa_info[] = {
	{
		.oem_id		= "KPSVVJ",
	}
};

static void ipmi_check_phytium_workaround(void)
{
#ifdef CONFIG_ACPI
	struct acpi_table_header tbl;
	int i;

	if (ACPI_FAILURE(acpi_get_table_header(ACPI_SIG_DSDT, 0, &tbl)))
		return;

	for (i = 0; i < ARRAY_SIZE(wa_info); i++) {
		if (strncmp(wa_info[i].oem_id, tbl.oem_id, ACPI_OEM_ID_SIZE))
			continue;

		apply_phytium2500_workaround = true;
		break;
	}
#endif
}

static void ctl_smc(unsigned long arg0, unsigned long arg1,
		    unsigned long arg2, unsigned long arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(arg0, arg1, arg2, arg3, 0, 0, 0, 0, &res);
	if (res.a0 != 0)
		pr_err("Error: Firmware call SMC reset Failed: %d, addr: 0x%lx\n",
			(int)res.a0, arg2);
}

static void ctl_timeout_reset(void)
{
	ctl_smc(CTL_RST_FUNC_ID, 0x1, 0x28100208, 0x1);
	ctl_smc(CTL_RST_FUNC_ID, 0x1, 0x2810020C, 0x1);
}

static inline void ipmi_phytium_workaround(void)
{
	if (apply_phytium2500_workaround)
		ctl_timeout_reset();
}

#else
static inline void ipmi_check_phytium_workaround(void) {}
static inline void ipmi_phytium_workaround(void) {}
#endif

static unsigned char intf_mem_inb(const struct si_sm_io *io,
				  unsigned int offset)
{
	ipmi_phytium_workaround();

	return readb((io->addr)+(offset * io->regspacing));
}

static void intf_mem_outb(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inw(const struct si_sm_io *io,
				  unsigned int offset)
{
	ipmi_phytium_workaround();

	return (readw((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outw(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inl(const struct si_sm_io *io,
				  unsigned int offset)
{
	ipmi_phytium_workaround();

	return (readl((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outl(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writel(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

#ifdef readq
static unsigned char mem_inq(const struct si_sm_io *io, unsigned int offset)
{
	ipmi_phytium_workaround();

	return (readq((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void mem_outq(const struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeq((u64)b << io->regshift, (io->addr)+(offset * io->regspacing));
}
#endif

static void mem_region_cleanup(struct si_sm_io *io, int num)
{
	unsigned long addr = io->addr_data;
	int idx;

	for (idx = 0; idx < num; idx++)
		release_mem_region(addr + idx * io->regspacing,
				   io->regsize);
}

static void mem_cleanup(struct si_sm_io *io)
{
	if (io->addr) {
		iounmap(io->addr);
		mem_region_cleanup(io, io->io_size);
	}
}

int ipmi_si_mem_setup(struct si_sm_io *io)
{
	unsigned long addr = io->addr_data;
	int           mapsize, idx;

	if (!addr)
		return -ENODEV;

	ipmi_check_phytium_workaround();

	/*
	 * Figure out the actual readb/readw/readl/etc routine to use based
	 * upon the register size.
	 */
	switch (io->regsize) {
	case 1:
		io->inputb = intf_mem_inb;
		io->outputb = intf_mem_outb;
		break;
	case 2:
		io->inputb = intf_mem_inw;
		io->outputb = intf_mem_outw;
		break;
	case 4:
		io->inputb = intf_mem_inl;
		io->outputb = intf_mem_outl;
		break;
#ifdef readq
	case 8:
		io->inputb = mem_inq;
		io->outputb = mem_outq;
		break;
#endif
	default:
		dev_warn(io->dev, "Invalid register size: %d\n",
			 io->regsize);
		return -EINVAL;
	}

	/*
	 * Some BIOSes reserve disjoint memory regions in their ACPI
	 * tables.  This causes problems when trying to request the
	 * entire region.  Therefore we must request each register
	 * separately.
	 */
	for (idx = 0; idx < io->io_size; idx++) {
		if (request_mem_region(addr + idx * io->regspacing,
				       io->regsize, SI_DEVICE_NAME) == NULL) {
			/* Undo allocations */
			mem_region_cleanup(io, idx);
			return -EIO;
		}
	}

	/*
	 * Calculate the total amount of memory to claim.  This is an
	 * unusual looking calculation, but it avoids claiming any
	 * more memory than it has to.  It will claim everything
	 * between the first address to the end of the last full
	 * register.
	 */
	mapsize = ((io->io_size * io->regspacing)
		   - (io->regspacing - io->regsize));
	io->addr = ioremap(addr, mapsize);
	if (io->addr == NULL) {
		mem_region_cleanup(io, io->io_size);
		return -EIO;
	}

	io->io_cleanup = mem_cleanup;

	return 0;
}
