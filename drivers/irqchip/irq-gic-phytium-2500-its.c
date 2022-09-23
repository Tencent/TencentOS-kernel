/*
 * Copyright (C) 2020 Phytium Corporation.
 * Author: Wang Yinfeng <wangyinfeng@phytium.com.cn>
 *         Chen Baozi <chenbaozi@phytium.com.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitmap.h>
#include <linux/cpu.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/efi.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-phytium-2500.h>
#include <linux/irqchip/arm-gic-v4.h>

#include <asm/cputype.h>
#include <asm/exception.h>

#include "irq-gic-common.h"

#define ITS_FLAGS_CMDQ_NEEDS_FLUSHING		(1ULL << 0)
#define ITS_FLAGS_WORKAROUND_CAVIUM_22375	(1ULL << 1)
#define ITS_FLAGS_WORKAROUND_CAVIUM_23144	(1ULL << 2)
#define ITS_FLAGS_SAVE_SUSPEND_STATE		(1ULL << 3)

#define RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING	(1 << 0)
#define RDIST_FLAGS_RD_TABLES_PREALLOCATED	(1 << 1)

static u32 lpi_id_bits;

/*
 * We allocate memory for PROPBASE to cover 2 ^ lpi_id_bits LPIs to
 * deal with (one configuration byte per interrupt). PENDBASE has to
 * be 64kB aligned (one bit per LPI, plus 8192 bits for SPI/PPI/SGI).
 */
#define LPI_NRBITS		lpi_id_bits
#define LPI_PROPBASE_SZ		ALIGN(BIT(LPI_NRBITS), SZ_64K)
#define LPI_PENDBASE_SZ		ALIGN(BIT(LPI_NRBITS) / 8, SZ_64K)

#define LPI_PROP_DEFAULT_PRIO	GICD_INT_DEF_PRI

/*
 * Collection structure - just an ID, and a redistributor address to
 * ping. We use one per CPU as a bag of interrupts assigned to this
 * CPU.
 */
struct its_collection {
	u64			target_address;
	u16			col_id;
};

/*
 * The ITS_BASER structure - contains memory information, cached
 * value of BASER register configuration and ITS page size.
 */
struct its_baser {
	void		*base;
	u64		val;
	u32		order;
	u32		psz;
};

struct its_device;

/*
 * The ITS structure - contains most of the infrastructure, with the
 * top-level MSI domain, the command queue, the collections, and the
 * list of devices writing to it.
 *
 * dev_alloc_lock has to be taken for device allocations, while the
 * spinlock must be taken to parse data structures such as the device
 * list.
 */
struct its_node {
	raw_spinlock_t		lock;
	struct mutex		dev_alloc_lock;
	struct list_head	entry;
	void __iomem		*base;
	phys_addr_t		phys_base;
	struct its_cmd_block	*cmd_base;
	struct its_cmd_block	*cmd_write;
	struct its_baser	tables[GITS_BASER_NR_REGS];
	struct its_collection	*collections;
	struct fwnode_handle	*fwnode_handle;
	u64			(*get_msi_base)(struct its_device *its_dev);
	u64			cbaser_save;
	u32			ctlr_save;
	struct list_head	its_device_list;
	u64			flags;
	unsigned long		list_nr;
	u32			ite_size;
	u32			device_ids;
	int			numa_node;
	unsigned int		msi_domain_flags;
	u32			pre_its_base; /* for Socionext Synquacer */
	bool			is_v4;
	int			vlpi_redist_offset;
};

#define ITS_ITT_ALIGN		SZ_256

/* The maximum number of VPEID bits supported by VLPI commands */
#define ITS_MAX_VPEID_BITS	(16)
#define ITS_MAX_VPEID		(1 << (ITS_MAX_VPEID_BITS))

/* Convert page order to size in bytes */
#define PAGE_ORDER_TO_SIZE(o)	(PAGE_SIZE << (o))

struct event_lpi_map {
	unsigned long		*lpi_map;
	u16			*col_map;
	irq_hw_number_t		lpi_base;
	int			nr_lpis;
	struct mutex		vlpi_lock;
	struct its_vm		*vm;
	struct its_vlpi_map	*vlpi_maps;
	int			nr_vlpis;
};

/*
 * The ITS view of a device - belongs to an ITS, owns an interrupt
 * translation table, and a list of interrupts.  If it some of its
 * LPIs are injected into a guest (GICv4), the event_map.vm field
 * indicates which one.
 */
struct its_device {
	struct list_head	entry;
	struct its_node		*its;
	struct event_lpi_map	event_map;
	void			*itt;
	u32			nr_ites;
	u32			device_id;
	bool			shared;
};

static struct {
	raw_spinlock_t		lock;
	struct its_device	*dev;
	struct its_vpe		**vpes;
	int			next_victim;
} vpe_proxy;

static LIST_HEAD(its_nodes);
static DEFINE_RAW_SPINLOCK(its_lock);
static struct rdists *gic_rdists;
static struct irq_domain *its_parent;

static unsigned long its_list_map;
static u16 vmovp_seq_num;
static DEFINE_RAW_SPINLOCK(vmovp_lock);

static DEFINE_IDA(its_vpeid_ida);

#define gic_data_rdist()		(raw_cpu_ptr(gic_rdists->rdist))
#define gic_data_rdist_cpu(cpu)		(per_cpu_ptr(gic_rdists->rdist, cpu))
#define gic_data_rdist_rd_base()	(gic_data_rdist()->rd_base)
#define gic_data_rdist_vlpi_base()	(gic_data_rdist_rd_base() + SZ_128K)

static u16 get_its_list(struct its_vm *vm)
{
	struct its_node *its;
	unsigned long its_list = 0;

	list_for_each_entry(its, &its_nodes, entry) {
		if (!its->is_v4)
			continue;

		if (vm->vlpi_count[its->list_nr])
			__set_bit(its->list_nr, &its_list);
	}

	return (u16)its_list;
}

static struct its_collection *dev_event_to_col(struct its_device *its_dev,
					       u32 event)
{
	struct its_node *its = its_dev->its;

	return its->collections + its_dev->event_map.col_map[event];
}

static struct its_collection *valid_col(struct its_collection *col)
{
	if (WARN_ON_ONCE(col->target_address & GENMASK_ULL(15, 0)))
		return NULL;

	return col;
}

static struct its_vpe *valid_vpe(struct its_node *its, struct its_vpe *vpe)
{
	if (valid_col(its->collections + vpe->col_idx))
		return vpe;

	return NULL;
}

/*
 * ITS command descriptors - parameters to be encoded in a command
 * block.
 */
struct its_cmd_desc {
	union {
		struct {
			struct its_device *dev;
			u32 event_id;
		} its_inv_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_clear_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_int_cmd;

		struct {
			struct its_device *dev;
			int valid;
		} its_mapd_cmd;

		struct {
			struct its_collection *col;
			int valid;
		} its_mapc_cmd;

		struct {
			struct its_device *dev;
			u32 phys_id;
			u32 event_id;
		} its_mapti_cmd;

		struct {
			struct its_device *dev;
			struct its_collection *col;
			u32 event_id;
		} its_movi_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_discard_cmd;

		struct {
			struct its_collection *col;
		} its_invall_cmd;

		struct {
			struct its_vpe *vpe;
		} its_vinvall_cmd;

		struct {
			struct its_vpe *vpe;
			struct its_collection *col;
			bool valid;
		} its_vmapp_cmd;

		struct {
			struct its_vpe *vpe;
			struct its_device *dev;
			u32 virt_id;
			u32 event_id;
			bool db_enabled;
		} its_vmapti_cmd;

		struct {
			struct its_vpe *vpe;
			struct its_device *dev;
			u32 event_id;
			bool db_enabled;
		} its_vmovi_cmd;

		struct {
			struct its_vpe *vpe;
			struct its_collection *col;
			u16 seq_num;
			u16 its_list;
		} its_vmovp_cmd;
	};
};

/*
 * The ITS command block, which is what the ITS actually parses.
 */
struct its_cmd_block {
	u64	raw_cmd[4];
};

#define ITS_CMD_QUEUE_SZ		SZ_64K
#define ITS_CMD_QUEUE_NR_ENTRIES	(ITS_CMD_QUEUE_SZ / sizeof(struct its_cmd_block))

typedef struct its_collection *(*its_cmd_builder_t)(struct its_node *,
						    struct its_cmd_block *,
						    struct its_cmd_desc *);

typedef struct its_vpe *(*its_cmd_vbuilder_t)(struct its_node *,
					      struct its_cmd_block *,
					      struct its_cmd_desc *);

static void its_mask_encode(u64 *raw_cmd, u64 val, int h, int l)
{
	u64 mask = GENMASK_ULL(h, l);
	*raw_cmd &= ~mask;
	*raw_cmd |= (val << l) & mask;
}

static void its_encode_cmd(struct its_cmd_block *cmd, u8 cmd_nr)
{
	its_mask_encode(&cmd->raw_cmd[0], cmd_nr, 7, 0);
}

static void its_encode_devid(struct its_cmd_block *cmd, u32 devid)
{
	its_mask_encode(&cmd->raw_cmd[0], devid, 63, 32);
}

static void its_encode_event_id(struct its_cmd_block *cmd, u32 id)
{
	its_mask_encode(&cmd->raw_cmd[1], id, 31, 0);
}

static void its_encode_phys_id(struct its_cmd_block *cmd, u32 phys_id)
{
	its_mask_encode(&cmd->raw_cmd[1], phys_id, 63, 32);
}

static void its_encode_size(struct its_cmd_block *cmd, u8 size)
{
	its_mask_encode(&cmd->raw_cmd[1], size, 4, 0);
}

static void its_encode_itt(struct its_cmd_block *cmd, u64 itt_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], itt_addr >> 8, 51, 8);
}

static void its_encode_valid(struct its_cmd_block *cmd, int valid)
{
	its_mask_encode(&cmd->raw_cmd[2], !!valid, 63, 63);
}

static void its_encode_target(struct its_cmd_block *cmd, u64 target_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], target_addr >> 16, 51, 16);
}

static void its_encode_collection(struct its_cmd_block *cmd, u16 col)
{
	its_mask_encode(&cmd->raw_cmd[2], col, 15, 0);
}

static void its_encode_vpeid(struct its_cmd_block *cmd, u16 vpeid)
{
	its_mask_encode(&cmd->raw_cmd[1], vpeid, 47, 32);
}

static void its_encode_virt_id(struct its_cmd_block *cmd, u32 virt_id)
{
	its_mask_encode(&cmd->raw_cmd[2], virt_id, 31, 0);
}

static void its_encode_db_phys_id(struct its_cmd_block *cmd, u32 db_phys_id)
{
	its_mask_encode(&cmd->raw_cmd[2], db_phys_id, 63, 32);
}

static void its_encode_db_valid(struct its_cmd_block *cmd, bool db_valid)
{
	its_mask_encode(&cmd->raw_cmd[2], db_valid, 0, 0);
}

static void its_encode_seq_num(struct its_cmd_block *cmd, u16 seq_num)
{
	its_mask_encode(&cmd->raw_cmd[0], seq_num, 47, 32);
}

static void its_encode_its_list(struct its_cmd_block *cmd, u16 its_list)
{
	its_mask_encode(&cmd->raw_cmd[1], its_list, 15, 0);
}

static void its_encode_vpt_addr(struct its_cmd_block *cmd, u64 vpt_pa)
{
	its_mask_encode(&cmd->raw_cmd[3], vpt_pa >> 16, 51, 16);
}

static void its_encode_vpt_size(struct its_cmd_block *cmd, u8 vpt_size)
{
	its_mask_encode(&cmd->raw_cmd[3], vpt_size, 4, 0);
}

static inline void its_fixup_cmd(struct its_cmd_block *cmd)
{
	/* Let's fixup BE commands */
	cmd->raw_cmd[0] = cpu_to_le64(cmd->raw_cmd[0]);
	cmd->raw_cmd[1] = cpu_to_le64(cmd->raw_cmd[1]);
	cmd->raw_cmd[2] = cpu_to_le64(cmd->raw_cmd[2]);
	cmd->raw_cmd[3] = cpu_to_le64(cmd->raw_cmd[3]);
}

static struct its_collection *its_build_mapd_cmd(struct its_node *its,
						 struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	unsigned long itt_addr;
	u8 size = ilog2(desc->its_mapd_cmd.dev->nr_ites);

	itt_addr = virt_to_phys(desc->its_mapd_cmd.dev->itt);
	itt_addr = ALIGN(itt_addr, ITS_ITT_ALIGN);

	its_encode_cmd(cmd, GITS_CMD_MAPD);
	its_encode_devid(cmd, desc->its_mapd_cmd.dev->device_id);
	its_encode_size(cmd, size - 1);
	its_encode_itt(cmd, itt_addr);
	its_encode_valid(cmd, desc->its_mapd_cmd.valid);

	its_fixup_cmd(cmd);

	return NULL;
}

static struct its_collection *its_build_mapc_cmd(struct its_node *its,
						 struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	its_encode_cmd(cmd, GITS_CMD_MAPC);
	its_encode_collection(cmd, desc->its_mapc_cmd.col->col_id);
	its_encode_target(cmd, desc->its_mapc_cmd.col->target_address);
	its_encode_valid(cmd, desc->its_mapc_cmd.valid);

	its_fixup_cmd(cmd);

	return desc->its_mapc_cmd.col;
}

static struct its_collection *its_build_mapti_cmd(struct its_node *its,
						  struct its_cmd_block *cmd,
						  struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_mapti_cmd.dev,
			       desc->its_mapti_cmd.event_id);
	col->col_id = col->col_id % 64;

	its_encode_cmd(cmd, GITS_CMD_MAPTI);
	its_encode_devid(cmd, desc->its_mapti_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_mapti_cmd.event_id);
	its_encode_phys_id(cmd, desc->its_mapti_cmd.phys_id);
	its_encode_collection(cmd, col->col_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_movi_cmd(struct its_node *its,
						 struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_movi_cmd.dev,
			       desc->its_movi_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_MOVI);
	its_encode_devid(cmd, desc->its_movi_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_movi_cmd.event_id);
	its_encode_collection(cmd, desc->its_movi_cmd.col->col_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_discard_cmd(struct its_node *its,
						    struct its_cmd_block *cmd,
						    struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_discard_cmd.dev,
			       desc->its_discard_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_DISCARD);
	its_encode_devid(cmd, desc->its_discard_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_discard_cmd.event_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_inv_cmd(struct its_node *its,
						struct its_cmd_block *cmd,
						struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_inv_cmd.dev,
			       desc->its_inv_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_INV);
	its_encode_devid(cmd, desc->its_inv_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_inv_cmd.event_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_int_cmd(struct its_node *its,
						struct its_cmd_block *cmd,
						struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_int_cmd.dev,
			       desc->its_int_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_INT);
	its_encode_devid(cmd, desc->its_int_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_int_cmd.event_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_clear_cmd(struct its_node *its,
						  struct its_cmd_block *cmd,
						  struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_clear_cmd.dev,
			       desc->its_clear_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_CLEAR);
	its_encode_devid(cmd, desc->its_clear_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_clear_cmd.event_id);

	its_fixup_cmd(cmd);

	return valid_col(col);
}

static struct its_collection *its_build_invall_cmd(struct its_node *its,
						   struct its_cmd_block *cmd,
						   struct its_cmd_desc *desc)
{
	its_encode_cmd(cmd, GITS_CMD_INVALL);
	its_encode_collection(cmd, desc->its_mapc_cmd.col->col_id);

	its_fixup_cmd(cmd);

	return NULL;
}

static struct its_vpe *its_build_vinvall_cmd(struct its_node *its,
					     struct its_cmd_block *cmd,
					     struct its_cmd_desc *desc)
{
	its_encode_cmd(cmd, GITS_CMD_VINVALL);
	its_encode_vpeid(cmd, desc->its_vinvall_cmd.vpe->vpe_id);

	its_fixup_cmd(cmd);

	return valid_vpe(its, desc->its_vinvall_cmd.vpe);
}

static struct its_vpe *its_build_vmapp_cmd(struct its_node *its,
					   struct its_cmd_block *cmd,
					   struct its_cmd_desc *desc)
{
	unsigned long vpt_addr;
	u64 target;

	vpt_addr = virt_to_phys(page_address(desc->its_vmapp_cmd.vpe->vpt_page));
	target = desc->its_vmapp_cmd.col->target_address + its->vlpi_redist_offset;

	its_encode_cmd(cmd, GITS_CMD_VMAPP);
	its_encode_vpeid(cmd, desc->its_vmapp_cmd.vpe->vpe_id);
	its_encode_valid(cmd, desc->its_vmapp_cmd.valid);
	its_encode_target(cmd, target);
	its_encode_vpt_addr(cmd, vpt_addr);
	its_encode_vpt_size(cmd, LPI_NRBITS - 1);

	its_fixup_cmd(cmd);

	return valid_vpe(its, desc->its_vmapp_cmd.vpe);
}

static struct its_vpe *its_build_vmapti_cmd(struct its_node *its,
					    struct its_cmd_block *cmd,
					    struct its_cmd_desc *desc)
{
	u32 db;

	if (desc->its_vmapti_cmd.db_enabled)
		db = desc->its_vmapti_cmd.vpe->vpe_db_lpi;
	else
		db = 1023;

	its_encode_cmd(cmd, GITS_CMD_VMAPTI);
	its_encode_devid(cmd, desc->its_vmapti_cmd.dev->device_id);
	its_encode_vpeid(cmd, desc->its_vmapti_cmd.vpe->vpe_id);
	its_encode_event_id(cmd, desc->its_vmapti_cmd.event_id);
	its_encode_db_phys_id(cmd, db);
	its_encode_virt_id(cmd, desc->its_vmapti_cmd.virt_id);

	its_fixup_cmd(cmd);

	return valid_vpe(its, desc->its_vmapti_cmd.vpe);
}

static struct its_vpe *its_build_vmovi_cmd(struct its_node *its,
					   struct its_cmd_block *cmd,
					   struct its_cmd_desc *desc)
{
	u32 db;

	if (desc->its_vmovi_cmd.db_enabled)
		db = desc->its_vmovi_cmd.vpe->vpe_db_lpi;
	else
		db = 1023;

	its_encode_cmd(cmd, GITS_CMD_VMOVI);
	its_encode_devid(cmd, desc->its_vmovi_cmd.dev->device_id);
	its_encode_vpeid(cmd, desc->its_vmovi_cmd.vpe->vpe_id);
	its_encode_event_id(cmd, desc->its_vmovi_cmd.event_id);
	its_encode_db_phys_id(cmd, db);
	its_encode_db_valid(cmd, true);

	its_fixup_cmd(cmd);

	return valid_vpe(its, desc->its_vmovi_cmd.vpe);
}

static struct its_vpe *its_build_vmovp_cmd(struct its_node *its,
					   struct its_cmd_block *cmd,
					   struct its_cmd_desc *desc)
{
	u64 target;

	target = desc->its_vmovp_cmd.col->target_address + its->vlpi_redist_offset;
	its_encode_cmd(cmd, GITS_CMD_VMOVP);
	its_encode_seq_num(cmd, desc->its_vmovp_cmd.seq_num);
	its_encode_its_list(cmd, desc->its_vmovp_cmd.its_list);
	its_encode_vpeid(cmd, desc->its_vmovp_cmd.vpe->vpe_id);
	its_encode_target(cmd, target);

	its_fixup_cmd(cmd);

	return valid_vpe(its, desc->its_vmovp_cmd.vpe);
}

static u64 its_cmd_ptr_to_offset(struct its_node *its,
				 struct its_cmd_block *ptr)
{
	return (ptr - its->cmd_base) * sizeof(*ptr);
}

static int its_queue_full(struct its_node *its)
{
	int widx;
	int ridx;

	widx = its->cmd_write - its->cmd_base;
	ridx = readl_relaxed(its->base + GITS_CREADR) / sizeof(struct its_cmd_block);

	/* This is incredibly unlikely to happen, unless the ITS locks up. */
	if (((widx + 1) % ITS_CMD_QUEUE_NR_ENTRIES) == ridx)
		return 1;

	return 0;
}

static struct its_cmd_block *its_allocate_entry(struct its_node *its)
{
	struct its_cmd_block *cmd;
	u32 count = 1000000;	/* 1s! */

	while (its_queue_full(its)) {
		count--;
		if (!count) {
			pr_err_ratelimited("ITS queue not draining\n");
			return NULL;
		}
		cpu_relax();
		udelay(1);
	}

	cmd = its->cmd_write++;

	/* Handle queue wrapping */
	if (its->cmd_write == (its->cmd_base + ITS_CMD_QUEUE_NR_ENTRIES))
		its->cmd_write = its->cmd_base;

	/* Clear command  */
	cmd->raw_cmd[0] = 0;
	cmd->raw_cmd[1] = 0;
	cmd->raw_cmd[2] = 0;
	cmd->raw_cmd[3] = 0;

	return cmd;
}

static struct its_cmd_block *its_post_commands(struct its_node *its)
{
	u64 wr = its_cmd_ptr_to_offset(its, its->cmd_write);

	writel_relaxed(wr, its->base + GITS_CWRITER);

	return its->cmd_write;
}

static void its_flush_cmd(struct its_node *its, struct its_cmd_block *cmd)
{
	/*
	 * Make sure the commands written to memory are observable by
	 * the ITS.
	 */
	if (its->flags & ITS_FLAGS_CMDQ_NEEDS_FLUSHING)
		gic_flush_dcache_to_poc(cmd, sizeof(*cmd));
	else
		dsb(ishst);
}

static int its_wait_for_range_completion(struct its_node *its,
					 u64	prev_idx,
					 struct its_cmd_block *to)
{
	u64 rd_idx, to_idx, linear_idx;
	u32 count = 1000000;	/* 1s! */

	/* Linearize to_idx if the command set has wrapped around */
	to_idx = its_cmd_ptr_to_offset(its, to);
	if (to_idx < prev_idx)
		to_idx += ITS_CMD_QUEUE_SZ;

	linear_idx = prev_idx;

	while (1) {
		s64 delta;

		rd_idx = readl_relaxed(its->base + GITS_CREADR);

		/*
		 * Compute the read pointer progress, taking the
		 * potential wrap-around into account.
		 */
		delta = rd_idx - prev_idx;
		if (rd_idx < prev_idx)
			delta += ITS_CMD_QUEUE_SZ;

		linear_idx += delta;
		if (linear_idx >= to_idx)
			break;

		count--;
		if (!count) {
			pr_err_ratelimited("ITS queue timeout (%llu %llu)\n",
					   to_idx, linear_idx);
			return -1;
		}
		prev_idx = rd_idx;
		cpu_relax();
		udelay(1);
	}

	return 0;
}

/* Warning, macro hell follows */
#define BUILD_SINGLE_CMD_FUNC(name, buildtype, synctype, buildfn)	\
void name(struct its_node *its,						\
	  buildtype builder,						\
	  struct its_cmd_desc *desc)					\
{									\
	struct its_cmd_block *cmd, *sync_cmd, *next_cmd;		\
	synctype *sync_obj;						\
	unsigned long flags;						\
	u64 rd_idx;							\
									\
	raw_spin_lock_irqsave(&its->lock, flags);			\
									\
	cmd = its_allocate_entry(its);					\
	if (!cmd) {		/* We're soooooo screewed... */		\
		raw_spin_unlock_irqrestore(&its->lock, flags);		\
		return;							\
	}								\
	sync_obj = builder(its, cmd, desc);				\
	its_flush_cmd(its, cmd);					\
									\
	if (sync_obj) {							\
		sync_cmd = its_allocate_entry(its);			\
		if (!sync_cmd)						\
			goto post;					\
									\
		buildfn(its, sync_cmd, sync_obj);			\
		its_flush_cmd(its, sync_cmd);				\
	}								\
									\
post:									\
	rd_idx = readl_relaxed(its->base + GITS_CREADR);		\
	next_cmd = its_post_commands(its);				\
	raw_spin_unlock_irqrestore(&its->lock, flags);			\
									\
	if (its_wait_for_range_completion(its, rd_idx, next_cmd))	\
		pr_err_ratelimited("ITS cmd %ps failed\n", builder);	\
}

static void its_build_sync_cmd(struct its_node *its,
			       struct its_cmd_block *sync_cmd,
			       struct its_collection *sync_col)
{
	its_encode_cmd(sync_cmd, GITS_CMD_SYNC);
	its_encode_target(sync_cmd, sync_col->target_address);

	its_fixup_cmd(sync_cmd);
}

static BUILD_SINGLE_CMD_FUNC(its_send_single_command, its_cmd_builder_t,
			     struct its_collection, its_build_sync_cmd)

static void its_build_vsync_cmd(struct its_node *its,
				struct its_cmd_block *sync_cmd,
				struct its_vpe *sync_vpe)
{
	its_encode_cmd(sync_cmd, GITS_CMD_VSYNC);
	its_encode_vpeid(sync_cmd, sync_vpe->vpe_id);

	its_fixup_cmd(sync_cmd);
}

static BUILD_SINGLE_CMD_FUNC(its_send_single_vcommand, its_cmd_vbuilder_t,
			     struct its_vpe, its_build_vsync_cmd)

static void its_send_int(struct its_device *dev, u32 event_id)
{
	struct its_cmd_desc desc;

	desc.its_int_cmd.dev = dev;
	desc.its_int_cmd.event_id = event_id;

	its_send_single_command(dev->its, its_build_int_cmd, &desc);
}

static void its_send_clear(struct its_device *dev, u32 event_id)
{
	struct its_cmd_desc desc;

	desc.its_clear_cmd.dev = dev;
	desc.its_clear_cmd.event_id = event_id;

	its_send_single_command(dev->its, its_build_clear_cmd, &desc);
}

static void its_send_inv(struct its_device *dev, u32 event_id)
{
	struct its_cmd_desc desc;

	desc.its_inv_cmd.dev = dev;
	desc.its_inv_cmd.event_id = event_id;

	its_send_single_command(dev->its, its_build_inv_cmd, &desc);
}

static void its_send_mapd(struct its_device *dev, int valid)
{
	struct its_cmd_desc desc;

	desc.its_mapd_cmd.dev = dev;
	desc.its_mapd_cmd.valid = !!valid;

	its_send_single_command(dev->its, its_build_mapd_cmd, &desc);
}

static void its_send_mapc(struct its_node *its, struct its_collection *col,
			  int valid)
{
	struct its_cmd_desc desc;

	desc.its_mapc_cmd.col = col;
	desc.its_mapc_cmd.valid = !!valid;

	its_send_single_command(its, its_build_mapc_cmd, &desc);
}

static void its_send_mapti(struct its_device *dev, u32 irq_id, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_mapti_cmd.dev = dev;
	desc.its_mapti_cmd.phys_id = irq_id;
	desc.its_mapti_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_mapti_cmd, &desc);
}

static void its_send_movi(struct its_device *dev,
			  struct its_collection *col, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_movi_cmd.dev = dev;
	desc.its_movi_cmd.col = col;
	desc.its_movi_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_movi_cmd, &desc);
}

static void its_send_discard(struct its_device *dev, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_discard_cmd.dev = dev;
	desc.its_discard_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_discard_cmd, &desc);
}

static void its_send_invall(struct its_node *its, struct its_collection *col)
{
	struct its_cmd_desc desc;

	desc.its_invall_cmd.col = col;

	its_send_single_command(its, its_build_invall_cmd, &desc);
}

static void its_send_vmapti(struct its_device *dev, u32 id)
{
	struct its_vlpi_map *map = &dev->event_map.vlpi_maps[id];
	struct its_cmd_desc desc;

	desc.its_vmapti_cmd.vpe = map->vpe;
	desc.its_vmapti_cmd.dev = dev;
	desc.its_vmapti_cmd.virt_id = map->vintid;
	desc.its_vmapti_cmd.event_id = id;
	desc.its_vmapti_cmd.db_enabled = map->db_enabled;

	its_send_single_vcommand(dev->its, its_build_vmapti_cmd, &desc);
}

static void its_send_vmovi(struct its_device *dev, u32 id)
{
	struct its_vlpi_map *map = &dev->event_map.vlpi_maps[id];
	struct its_cmd_desc desc;

	desc.its_vmovi_cmd.vpe = map->vpe;
	desc.its_vmovi_cmd.dev = dev;
	desc.its_vmovi_cmd.event_id = id;
	desc.its_vmovi_cmd.db_enabled = map->db_enabled;

	its_send_single_vcommand(dev->its, its_build_vmovi_cmd, &desc);
}

static void its_send_vmapp(struct its_node *its,
			   struct its_vpe *vpe, bool valid)
{
	struct its_cmd_desc desc;

	desc.its_vmapp_cmd.vpe = vpe;
	desc.its_vmapp_cmd.valid = valid;
	desc.its_vmapp_cmd.col = &its->collections[vpe->col_idx];

	its_send_single_vcommand(its, its_build_vmapp_cmd, &desc);
}

static void its_send_vmovp(struct its_vpe *vpe)
{
	struct its_cmd_desc desc = {};
	struct its_node *its;
	unsigned long flags;
	int col_id = vpe->col_idx;

	desc.its_vmovp_cmd.vpe = vpe;

	if (!its_list_map) {
		its = list_first_entry(&its_nodes, struct its_node, entry);
		desc.its_vmovp_cmd.col = &its->collections[col_id];
		its_send_single_vcommand(its, its_build_vmovp_cmd, &desc);
		return;
	}

	/*
	 * Yet another marvel of the architecture. If using the
	 * its_list "feature", we need to make sure that all ITSs
	 * receive all VMOVP commands in the same order. The only way
	 * to guarantee this is to make vmovp a serialization point.
	 *
	 * Wall <-- Head.
	 */
	raw_spin_lock_irqsave(&vmovp_lock, flags);

	desc.its_vmovp_cmd.seq_num = vmovp_seq_num++;
	desc.its_vmovp_cmd.its_list = get_its_list(vpe->its_vm);

	/* Emit VMOVPs */
	list_for_each_entry(its, &its_nodes, entry) {
		if (!its->is_v4)
			continue;

		if (!vpe->its_vm->vlpi_count[its->list_nr])
			continue;

		desc.its_vmovp_cmd.col = &its->collections[col_id];
		its_send_single_vcommand(its, its_build_vmovp_cmd, &desc);
	}

	raw_spin_unlock_irqrestore(&vmovp_lock, flags);
}

static void its_send_vinvall(struct its_node *its, struct its_vpe *vpe)
{
	struct its_cmd_desc desc;

	desc.its_vinvall_cmd.vpe = vpe;
	its_send_single_vcommand(its, its_build_vinvall_cmd, &desc);
}

/*
 * irqchip functions - assumes MSI, mostly.
 */

static inline u32 its_get_event_id(struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	return d->hwirq - its_dev->event_map.lpi_base;
}

static void lpi_write_config(struct irq_data *d, u8 clr, u8 set)
{
	irq_hw_number_t hwirq;
	void *va;
	u8 *cfg;

	if (irqd_is_forwarded_to_vcpu(d)) {
		struct its_device *its_dev = irq_data_get_irq_chip_data(d);
		u32 event = its_get_event_id(d);
		struct its_vlpi_map *map;

		va = page_address(its_dev->event_map.vm->vprop_page);
		map = &its_dev->event_map.vlpi_maps[event];
		hwirq = map->vintid;

		/* Remember the updated property */
		map->properties &= ~clr;
		map->properties |= set | LPI_PROP_GROUP1;
	} else {
		va = gic_rdists->prop_table_va;
		hwirq = d->hwirq;
	}

	cfg = va + hwirq - 8192;
	*cfg &= ~clr;
	*cfg |= set | LPI_PROP_GROUP1;

	/*
	 * Make the above write visible to the redistributors.
	 * And yes, we're flushing exactly: One. Single. Byte.
	 * Humpf...
	 */
	if (gic_rdists->flags & RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING)
		gic_flush_dcache_to_poc(cfg, sizeof(*cfg));
	else
		dsb(ishst);
}

static void lpi_update_config(struct irq_data *d, u8 clr, u8 set)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);

	lpi_write_config(d, clr, set);
	its_send_inv(its_dev, its_get_event_id(d));
}

static void its_vlpi_set_doorbell(struct irq_data *d, bool enable)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);

	if (its_dev->event_map.vlpi_maps[event].db_enabled == enable)
		return;

	its_dev->event_map.vlpi_maps[event].db_enabled = enable;

	/*
	 * More fun with the architecture:
	 *
	 * Ideally, we'd issue a VMAPTI to set the doorbell to its LPI
	 * value or to 1023, depending on the enable bit. But that
	 * would be issueing a mapping for an /existing/ DevID+EventID
	 * pair, which is UNPREDICTABLE. Instead, let's issue a VMOVI
	 * to the /same/ vPE, using this opportunity to adjust the
	 * doorbell. Mouahahahaha. We loves it, Precious.
	 */
	its_send_vmovi(its_dev, event);
}

static void its_mask_irq(struct irq_data *d)
{
	if (irqd_is_forwarded_to_vcpu(d))
		its_vlpi_set_doorbell(d, false);

	lpi_update_config(d, LPI_PROP_ENABLED, 0);
}

static void its_unmask_irq(struct irq_data *d)
{
	if (irqd_is_forwarded_to_vcpu(d))
		its_vlpi_set_doorbell(d, true);

	lpi_update_config(d, 0, LPI_PROP_ENABLED);
}

#define MAX_MARS3_SKT_COUNT  8

static int its_cpumask_select(struct its_device *its_dev,
                            const struct cpumask *mask_val,
                            const struct cpumask *cpu_mask)
{
    unsigned int skt, skt_id, i;
    phys_addr_t its_phys_base;
    unsigned int cpu, cpus = 0;

    unsigned int skt_cpu_cnt[MAX_MARS3_SKT_COUNT] = {0};

    for (i = 0; i < nr_cpu_ids; i++) {
        skt = (cpu_logical_map(i) >> 16) & 0xff;
        if ((skt >= 0) && (skt < MAX_MARS3_SKT_COUNT)) {
            skt_cpu_cnt[skt]++;
        } else if (0xff != skt ) {
            pr_err("socket address: %d is out of range.", skt);
        }
    }

    its_phys_base = its_dev->its->phys_base;
    skt_id = (its_phys_base >> 41) & 0x7;

    if (0 != skt_id) {
        for (i = 0; i < skt_id; i++) {
            cpus += skt_cpu_cnt[i];
        }
    }

    cpu = cpumask_any_and(mask_val, cpu_mask);
    cpus = cpus + cpu % skt_cpu_cnt[skt_id];

    if (is_kdump_kernel()) {
         skt = (cpu_logical_map(cpu) >> 16) & 0xff;

         if (skt_id == skt)
              return cpu;

         for (i = 0; i < nr_cpu_ids; i++) {
              skt = (cpu_logical_map(i) >> 16) & 0xff;

              if ((skt >= 0) && (skt < MAX_MARS3_SKT_COUNT)) {
                   if(skt_id == skt)
                        return i;
              } else if (0xff != skt) {
                   pr_err("socket address: %d is out of range.", skt);
              }
         }
    }

    return cpus;
}

static int its_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			    bool force)
{
	unsigned int cpu;
	const struct cpumask *cpu_mask = cpu_online_mask;
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_collection *target_col;
	u32 id = its_get_event_id(d);

	/* A forwarded interrupt should use irq_set_vcpu_affinity */
	if (irqd_is_forwarded_to_vcpu(d))
		return -EINVAL;

       /* lpi cannot be routed to a redistributor that is on a foreign node */
	if (its_dev->its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_23144) {
		if (its_dev->its->numa_node >= 0) {
			cpu_mask = cpumask_of_node(its_dev->its->numa_node);
			if (!cpumask_intersects(mask_val, cpu_mask))
				return -EINVAL;
		}
	}


	cpu = its_cpumask_select(its_dev, mask_val, cpu_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	/* don't set the affinity when the target cpu is same as current one */
	if (cpu != its_dev->event_map.col_map[id]) {
		target_col = &its_dev->its->collections[cpu];
		its_send_movi(its_dev, target_col, id);
		its_dev->event_map.col_map[id] = cpu;
		irq_data_update_effective_affinity(d, cpumask_of(cpu));
	}

	return IRQ_SET_MASK_OK_DONE;
}

static u64 its_irq_get_msi_base(struct its_device *its_dev)
{
	struct its_node *its = its_dev->its;

	return its->phys_base + GITS_TRANSLATER;
}

static void its_irq_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_node *its;
	u64 addr;

	its = its_dev->its;
	addr = its->get_msi_base(its_dev);

	msg->address_lo		= lower_32_bits(addr);
	msg->address_hi		= upper_32_bits(addr);
	msg->data		= its_get_event_id(d);
}

static int its_irq_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which,
				     bool state)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	if (state)
		its_send_int(its_dev, event);
	else
		its_send_clear(its_dev, event);

	return 0;
}

static void its_map_vm(struct its_node *its, struct its_vm *vm)
{
	unsigned long flags;

	/* Not using the ITS list? Everything is always mapped. */
	if (!its_list_map)
		return;

	raw_spin_lock_irqsave(&vmovp_lock, flags);

	/*
	 * If the VM wasn't mapped yet, iterate over the vpes and get
	 * them mapped now.
	 */
	vm->vlpi_count[its->list_nr]++;

	if (vm->vlpi_count[its->list_nr] == 1) {
		int i;

		for (i = 0; i < vm->nr_vpes; i++) {
			struct its_vpe *vpe = vm->vpes[i];
			struct irq_data *d = irq_get_irq_data(vpe->irq);

			/* Map the VPE to the first possible CPU */
			vpe->col_idx = cpumask_first(cpu_online_mask);
			its_send_vmapp(its, vpe, true);
			its_send_vinvall(its, vpe);
			irq_data_update_effective_affinity(d, cpumask_of(vpe->col_idx));
		}
	}

	raw_spin_unlock_irqrestore(&vmovp_lock, flags);
}

static void its_unmap_vm(struct its_node *its, struct its_vm *vm)
{
	unsigned long flags;

	/* Not using the ITS list? Everything is always mapped. */
	if (!its_list_map)
		return;

	raw_spin_lock_irqsave(&vmovp_lock, flags);

	if (!--vm->vlpi_count[its->list_nr]) {
		int i;

		for (i = 0; i < vm->nr_vpes; i++)
			its_send_vmapp(its, vm->vpes[i], false);
	}

	raw_spin_unlock_irqrestore(&vmovp_lock, flags);
}

static int its_vlpi_map(struct irq_data *d, struct its_cmd_info *info)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);
	int ret = 0;

	if (!info->map)
		return -EINVAL;

	mutex_lock(&its_dev->event_map.vlpi_lock);

	if (!its_dev->event_map.vm) {
		struct its_vlpi_map *maps;

		maps = kcalloc(its_dev->event_map.nr_lpis, sizeof(*maps),
			       GFP_KERNEL);
		if (!maps) {
			ret = -ENOMEM;
			goto out;
		}

		its_dev->event_map.vm = info->map->vm;
		its_dev->event_map.vlpi_maps = maps;
	} else if (its_dev->event_map.vm != info->map->vm) {
		ret = -EINVAL;
		goto out;
	}

	/* Get our private copy of the mapping information */
	its_dev->event_map.vlpi_maps[event] = *info->map;

	if (irqd_is_forwarded_to_vcpu(d)) {
		/* Already mapped, move it around */
		its_send_vmovi(its_dev, event);
	} else {
		/* Ensure all the VPEs are mapped on this ITS */
		its_map_vm(its_dev->its, info->map->vm);

		/*
		 * Flag the interrupt as forwarded so that we can
		 * start poking the virtual property table.
		 */
		irqd_set_forwarded_to_vcpu(d);

		/* Write out the property to the prop table */
		lpi_write_config(d, 0xff, info->map->properties);

		/* Drop the physical mapping */
		its_send_discard(its_dev, event);

		/* and install the virtual one */
		its_send_vmapti(its_dev, event);

		/* Increment the number of VLPIs */
		its_dev->event_map.nr_vlpis++;
	}

out:
	mutex_unlock(&its_dev->event_map.vlpi_lock);
	return ret;
}

static int its_vlpi_get(struct irq_data *d, struct its_cmd_info *info)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);
	int ret = 0;

	mutex_lock(&its_dev->event_map.vlpi_lock);

	if (!its_dev->event_map.vm ||
	    !its_dev->event_map.vlpi_maps[event].vm) {
		ret = -EINVAL;
		goto out;
	}

	/* Copy our mapping information to the incoming request */
	*info->map = its_dev->event_map.vlpi_maps[event];

out:
	mutex_unlock(&its_dev->event_map.vlpi_lock);
	return ret;
}

static int its_vlpi_unmap(struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);
	int ret = 0;

	mutex_lock(&its_dev->event_map.vlpi_lock);

	if (!its_dev->event_map.vm || !irqd_is_forwarded_to_vcpu(d)) {
		ret = -EINVAL;
		goto out;
	}

	/* Drop the virtual mapping */
	its_send_discard(its_dev, event);

	/* and restore the physical one */
	irqd_clr_forwarded_to_vcpu(d);
	its_send_mapti(its_dev, d->hwirq, event);
	lpi_update_config(d, 0xff, (LPI_PROP_DEFAULT_PRIO |
				    LPI_PROP_ENABLED |
				    LPI_PROP_GROUP1));

	/* Potentially unmap the VM from this ITS */
	its_unmap_vm(its_dev->its, its_dev->event_map.vm);

	/*
	 * Drop the refcount and make the device available again if
	 * this was the last VLPI.
	 */
	if (!--its_dev->event_map.nr_vlpis) {
		its_dev->event_map.vm = NULL;
		kfree(its_dev->event_map.vlpi_maps);
	}

out:
	mutex_unlock(&its_dev->event_map.vlpi_lock);
	return ret;
}

static int its_vlpi_prop_update(struct irq_data *d, struct its_cmd_info *info)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);

	if (!its_dev->event_map.vm || !irqd_is_forwarded_to_vcpu(d))
		return -EINVAL;

	if (info->cmd_type == PROP_UPDATE_AND_INV_VLPI)
		lpi_update_config(d, 0xff, info->config);
	else
		lpi_write_config(d, 0xff, info->config);
	its_vlpi_set_doorbell(d, !!(info->config & LPI_PROP_ENABLED));

	return 0;
}

static int its_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu_info)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_cmd_info *info = vcpu_info;

	/* Need a v4 ITS */
	if (!its_dev->its->is_v4)
		return -EINVAL;

	/* Unmap request? */
	if (!info)
		return its_vlpi_unmap(d);

	switch (info->cmd_type) {
	case MAP_VLPI:
		return its_vlpi_map(d, info);

	case GET_VLPI:
		return its_vlpi_get(d, info);

	case PROP_UPDATE_VLPI:
	case PROP_UPDATE_AND_INV_VLPI:
		return its_vlpi_prop_update(d, info);

	default:
		return -EINVAL;
	}
}

static struct irq_chip its_irq_chip = {
	.name			= "ITS",
	.irq_mask		= its_mask_irq,
	.irq_unmask		= its_unmask_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= its_set_affinity,
	.irq_compose_msi_msg	= its_irq_compose_msi_msg,
	.irq_set_irqchip_state	= its_irq_set_irqchip_state,
	.irq_set_vcpu_affinity	= its_irq_set_vcpu_affinity,
};


/*
 * How we allocate LPIs:
 *
 * lpi_range_list contains ranges of LPIs that are to available to
 * allocate from. To allocate LPIs, just pick the first range that
 * fits the required allocation, and reduce it by the required
 * amount. Once empty, remove the range from the list.
 *
 * To free a range of LPIs, add a free range to the list, sort it and
 * merge the result if the new range happens to be adjacent to an
 * already free block.
 *
 * The consequence of the above is that allocation is cost is low, but
 * freeing is expensive. We assumes that freeing rarely occurs.
 */
#define ITS_MAX_LPI_NRBITS	16 /* 64K LPIs */

static DEFINE_MUTEX(lpi_range_lock);
static LIST_HEAD(lpi_range_list);

struct lpi_range {
	struct list_head	entry;
	u32			base_id;
	u32			span;
};

static struct lpi_range *mk_lpi_range(u32 base, u32 span)
{
	struct lpi_range *range;

	range = kmalloc(sizeof(*range), GFP_KERNEL);
	if (range) {
		range->base_id = base;
		range->span = span;
	}

	return range;
}

static int alloc_lpi_range(u32 nr_lpis, u32 *base)
{
	struct lpi_range *range, *tmp;
	int err = -ENOSPC;

	mutex_lock(&lpi_range_lock);

	list_for_each_entry_safe(range, tmp, &lpi_range_list, entry) {
		if (range->span >= nr_lpis) {
			*base = range->base_id;
			range->base_id += nr_lpis;
			range->span -= nr_lpis;

			if (range->span == 0) {
				list_del(&range->entry);
				kfree(range);
			}

			err = 0;
			break;
		}
	}

	mutex_unlock(&lpi_range_lock);

	pr_debug("ITS: alloc %u:%u\n", *base, nr_lpis);
	return err;
}

static void merge_lpi_ranges(struct lpi_range *a, struct lpi_range *b)
{
	if (&a->entry == &lpi_range_list || &b->entry == &lpi_range_list)
		return;
	if (a->base_id + a->span != b->base_id)
		return;
	b->base_id = a->base_id;
	b->span += a->span;
	list_del(&a->entry);
	kfree(a);
}

static int free_lpi_range(u32 base, u32 nr_lpis)
{
	struct lpi_range *new, *old;

	new = mk_lpi_range(base, nr_lpis);
	if (!new)
		return -ENOMEM;

	mutex_lock(&lpi_range_lock);

	list_for_each_entry_reverse(old, &lpi_range_list, entry) {
		if (old->base_id < base)
			break;
	}
	/*
	 * old is the last element with ->base_id smaller than base,
	 * so new goes right after it. If there are no elements with
	 * ->base_id smaller than base, &old->entry ends up pointing
	 * at the head of the list, and inserting new it the start of
	 * the list is the right thing to do in that case as well.
	 */
	list_add(&new->entry, &old->entry);
	/*
	 * Now check if we can merge with the preceding and/or
	 * following ranges.
	 */
	merge_lpi_ranges(old, new);
	merge_lpi_ranges(new, list_next_entry(new, entry));

	mutex_unlock(&lpi_range_lock);
	return 0;
}

static int __init its_lpi_init(u32 id_bits)
{
	u32 lpis = (1UL << id_bits) - 8192;
	u32 numlpis;
	int err;

	numlpis = 1UL << GICD_TYPER_NUM_LPIS(gic_rdists->gicd_typer);

	if (numlpis > 2 && !WARN_ON(numlpis > lpis)) {
		lpis = numlpis;
		pr_info("ITS: Using hypervisor restricted LPI range [%u]\n",
			lpis);
	}

	/*
	 * Initializing the allocator is just the same as freeing the
	 * full range of LPIs.
	 */
	err = free_lpi_range(8192, lpis);
	pr_debug("ITS: Allocator initialized for %u LPIs\n", lpis);
	return err;
}

static unsigned long *its_lpi_alloc(int nr_irqs, u32 *base, int *nr_ids)
{
	unsigned long *bitmap = NULL;
	int err = 0;

	do {
		err = alloc_lpi_range(nr_irqs, base);
		if (!err)
			break;

		nr_irqs /= 2;
	} while (nr_irqs > 0);

	if (!nr_irqs)
		err = -ENOSPC;

	if (err)
		goto out;

	bitmap = kcalloc(BITS_TO_LONGS(nr_irqs), sizeof (long), GFP_ATOMIC);
	if (!bitmap)
		goto out;

	*nr_ids = nr_irqs;

out:
	if (!bitmap)
		*base = *nr_ids = 0;

	return bitmap;
}

static void its_lpi_free(unsigned long *bitmap, u32 base, u32 nr_ids)
{
	WARN_ON(free_lpi_range(base, nr_ids));
	kfree(bitmap);
}

static void gic_reset_prop_table(void *va)
{
	/* Priority 0xa0, Group-1, disabled */
	memset(va, LPI_PROP_DEFAULT_PRIO | LPI_PROP_GROUP1, LPI_PROPBASE_SZ);

	/* Make sure the GIC will observe the written configuration */
	gic_flush_dcache_to_poc(va, LPI_PROPBASE_SZ);
}

static struct page *its_allocate_prop_table(gfp_t gfp_flags)
{
	struct page *prop_page;

	prop_page = alloc_pages(gfp_flags, get_order(LPI_PROPBASE_SZ));
	if (!prop_page)
		return NULL;

	gic_reset_prop_table(page_address(prop_page));

	return prop_page;
}

static void its_free_prop_table(struct page *prop_page)
{
	free_pages((unsigned long)page_address(prop_page),
		   get_order(LPI_PROPBASE_SZ));
}

static bool gic_check_reserved_range(phys_addr_t addr, unsigned long size)
{
	phys_addr_t start, end, addr_end;
	u64 i;

	/*
	 * We don't bother checking for a kdump kernel as by
	 * construction, the LPI tables are out of this kernel's
	 * memory map.
	 */
	if (is_kdump_kernel())
		return true;

	addr_end = addr + size - 1;

	for_each_reserved_mem_region(i, &start, &end) {
		if (addr >= start && addr_end <= end)
			return true;
	}

	/* Not found, not a good sign... */
	pr_warn("GIC-2500: Expected reserved range [%pa:%pa], not found\n",
		&addr, &addr_end);
	add_taint(TAINT_CRAP, LOCKDEP_STILL_OK);
	return false;
}

static int gic_reserve_range(phys_addr_t addr, unsigned long size)
{
	if (efi_enabled(EFI_CONFIG_TABLES))
		return efi_mem_reserve_persistent(addr, size);

	return 0;
}

static int __init its_setup_lpi_prop_table(void)
{
	if (gic_rdists->flags & RDIST_FLAGS_RD_TABLES_PREALLOCATED) {
		u64 val;

		val = gicr_read_propbaser(gic_data_rdist_rd_base() + GICR_PROPBASER);
		lpi_id_bits = (val & GICR_PROPBASER_IDBITS_MASK) + 1;

		gic_rdists->prop_table_pa = val & GENMASK_ULL(51, 12);
		gic_rdists->prop_table_va = memremap(gic_rdists->prop_table_pa,
						     LPI_PROPBASE_SZ,
						     MEMREMAP_WB);
		gic_reset_prop_table(gic_rdists->prop_table_va);
	} else {
		struct page *page;

		lpi_id_bits = min_t(u32,
				    GICD_TYPER_ID_BITS(gic_rdists->gicd_typer),
				    ITS_MAX_LPI_NRBITS);
		page = its_allocate_prop_table(GFP_NOWAIT);
		if (!page) {
			pr_err("Failed to allocate PROPBASE\n");
			return -ENOMEM;
		}

		gic_rdists->prop_table_pa = page_to_phys(page);
		gic_rdists->prop_table_va = page_address(page);
		WARN_ON(gic_reserve_range(gic_rdists->prop_table_pa,
					  LPI_PROPBASE_SZ));
	}

	pr_info("GIC-2500: using LPI property table @%pa\n",
		&gic_rdists->prop_table_pa);

	return its_lpi_init(lpi_id_bits);
}

static const char *its_base_type_string[] = {
	[GITS_BASER_TYPE_DEVICE]	= "Devices",
	[GITS_BASER_TYPE_VCPU]		= "Virtual CPUs",
	[GITS_BASER_TYPE_RESERVED3]	= "Reserved (3)",
	[GITS_BASER_TYPE_COLLECTION]	= "Interrupt Collections",
	[GITS_BASER_TYPE_RESERVED5] 	= "Reserved (5)",
	[GITS_BASER_TYPE_RESERVED6] 	= "Reserved (6)",
	[GITS_BASER_TYPE_RESERVED7] 	= "Reserved (7)",
};

static u64 its_read_baser(struct its_node *its, struct its_baser *baser)
{
	u32 idx = baser - its->tables;

	return gits_read_baser(its->base + GITS_BASER + (idx << 3));
}

static void its_write_baser(struct its_node *its, struct its_baser *baser,
			    u64 val)
{
	u32 idx = baser - its->tables;

	gits_write_baser(val, its->base + GITS_BASER + (idx << 3));
	baser->val = its_read_baser(its, baser);
}

static int its_setup_baser(struct its_node *its, struct its_baser *baser,
			   u64 cache, u64 shr, u32 psz, u32 order,
			   bool indirect)
{
	u64 val = its_read_baser(its, baser);
	u64 esz = GITS_BASER_ENTRY_SIZE(val);
	u64 type = GITS_BASER_TYPE(val);
	u64 baser_phys, tmp;
	u32 alloc_pages;
	struct page *page;
	void *base;

retry_alloc_baser:
	alloc_pages = (PAGE_ORDER_TO_SIZE(order) / psz);
	if (alloc_pages > GITS_BASER_PAGES_MAX) {
		pr_warn("ITS@%pa: %s too large, reduce ITS pages %u->%u\n",
			&its->phys_base, its_base_type_string[type],
			alloc_pages, GITS_BASER_PAGES_MAX);
		alloc_pages = GITS_BASER_PAGES_MAX;
		order = get_order(GITS_BASER_PAGES_MAX * psz);
	}

	page = alloc_pages_node(its->numa_node, GFP_KERNEL | __GFP_ZERO, order);
	if (!page)
		return -ENOMEM;

	base = (void *)page_address(page);
	baser_phys = virt_to_phys(base);

	/* Check if the physical address of the memory is above 48bits */
	if (IS_ENABLED(CONFIG_ARM64_64K_PAGES) && (baser_phys >> 48)) {

		/* 52bit PA is supported only when PageSize=64K */
		if (psz != SZ_64K) {
			pr_err("ITS: no 52bit PA support when psz=%d\n", psz);
			free_pages((unsigned long)base, order);
			return -ENXIO;
		}

		/* Convert 52bit PA to 48bit field */
		baser_phys = GITS_BASER_PHYS_52_to_48(baser_phys);
	}

retry_baser:
	val = (baser_phys					 |
		(type << GITS_BASER_TYPE_SHIFT)			 |
		((esz - 1) << GITS_BASER_ENTRY_SIZE_SHIFT)	 |
		((alloc_pages - 1) << GITS_BASER_PAGES_SHIFT)	 |
		cache						 |
		shr						 |
		GITS_BASER_VALID);

	val |=	indirect ? GITS_BASER_INDIRECT : 0x0;

	switch (psz) {
	case SZ_4K:
		val |= GITS_BASER_PAGE_SIZE_4K;
		break;
	case SZ_16K:
		val |= GITS_BASER_PAGE_SIZE_16K;
		break;
	case SZ_64K:
		val |= GITS_BASER_PAGE_SIZE_64K;
		break;
	}

	its_write_baser(its, baser, val);
	tmp = baser->val;

	if ((val ^ tmp) & GITS_BASER_SHAREABILITY_MASK) {
		/*
		 * Shareability didn't stick. Just use
		 * whatever the read reported, which is likely
		 * to be the only thing this redistributor
		 * supports. If that's zero, make it
		 * non-cacheable as well.
		 */
		shr = tmp & GITS_BASER_SHAREABILITY_MASK;
		if (!shr) {
			cache = GITS_BASER_nC;
			gic_flush_dcache_to_poc(base, PAGE_ORDER_TO_SIZE(order));
		}
		goto retry_baser;
	}

	if ((val ^ tmp) & GITS_BASER_PAGE_SIZE_MASK) {
		/*
		 * Page size didn't stick. Let's try a smaller
		 * size and retry. If we reach 4K, then
		 * something is horribly wrong...
		 */
		free_pages((unsigned long)base, order);
		baser->base = NULL;

		switch (psz) {
		case SZ_16K:
			psz = SZ_4K;
			goto retry_alloc_baser;
		case SZ_64K:
			psz = SZ_16K;
			goto retry_alloc_baser;
		}
	}

	if (val != tmp) {
		pr_err("ITS@%pa: %s doesn't stick: %llx %llx\n",
		       &its->phys_base, its_base_type_string[type],
		       val, tmp);
		free_pages((unsigned long)base, order);
		return -ENXIO;
	}

	baser->order = order;
	baser->base = base;
	baser->psz = psz;
	tmp = indirect ? GITS_LVL1_ENTRY_SIZE : esz;

	pr_info("ITS@%pa: allocated %d %s @%lx (%s, esz %d, psz %dK, shr %d)\n",
		&its->phys_base, (int)(PAGE_ORDER_TO_SIZE(order) / (int)tmp),
		its_base_type_string[type],
		(unsigned long)virt_to_phys(base),
		indirect ? "indirect" : "flat", (int)esz,
		psz / SZ_1K, (int)shr >> GITS_BASER_SHAREABILITY_SHIFT);

	return 0;
}

static bool its_parse_indirect_baser(struct its_node *its,
				     struct its_baser *baser,
				     u32 psz, u32 *order, u32 ids)
{
	u64 tmp = its_read_baser(its, baser);
	u64 type = GITS_BASER_TYPE(tmp);
	u64 esz = GITS_BASER_ENTRY_SIZE(tmp);
	u64 val = GITS_BASER_InnerShareable | GITS_BASER_RaWaWb;
	u32 new_order = *order;
	bool indirect = false;

	/* No need to enable Indirection if memory requirement < (psz*2)bytes */
	if ((esz << ids) > (psz * 2)) {
		/*
		 * Find out whether hw supports a single or two-level table by
		 * table by reading bit at offset '62' after writing '1' to it.
		 */
		its_write_baser(its, baser, val | GITS_BASER_INDIRECT);
		indirect = !!(baser->val & GITS_BASER_INDIRECT);

		if (indirect) {
			/*
			 * The size of the lvl2 table is equal to ITS page size
			 * which is 'psz'. For computing lvl1 table size,
			 * subtract ID bits that sparse lvl2 table from 'ids'
			 * which is reported by ITS hardware times lvl1 table
			 * entry size.
			 */
			ids -= ilog2(psz / (int)esz);
			esz = GITS_LVL1_ENTRY_SIZE;
		}
	}

	/*
	 * Allocate as many entries as required to fit the
	 * range of device IDs that the ITS can grok... The ID
	 * space being incredibly sparse, this results in a
	 * massive waste of memory if two-level device table
	 * feature is not supported by hardware.
	 */
	new_order = max_t(u32, get_order(esz << ids), new_order);
	if (new_order >= MAX_ORDER) {
		new_order = MAX_ORDER - 1;
		ids = ilog2(PAGE_ORDER_TO_SIZE(new_order) / (int)esz);
		pr_warn("ITS@%pa: %s Table too large, reduce ids %u->%u\n",
			&its->phys_base, its_base_type_string[type],
			its->device_ids, ids);
	}

	*order = new_order;

	return indirect;
}

static void its_free_tables(struct its_node *its)
{
	int i;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		if (its->tables[i].base) {
			free_pages((unsigned long)its->tables[i].base,
				   its->tables[i].order);
			its->tables[i].base = NULL;
		}
	}
}

static int its_alloc_tables(struct its_node *its)
{
	u64 shr = GITS_BASER_InnerShareable;
	u64 cache = GITS_BASER_RaWaWb;
	u32 psz = SZ_64K;
	int err, i;

	if (its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_22375)
		/* erratum 24313: ignore memory access type */
		cache = GITS_BASER_nCnB;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		struct its_baser *baser = its->tables + i;
		u64 val = its_read_baser(its, baser);
		u64 type = GITS_BASER_TYPE(val);
		u32 order = get_order(psz);
		bool indirect = false;

		switch (type) {
		case GITS_BASER_TYPE_NONE:
			continue;

		case GITS_BASER_TYPE_DEVICE:
			indirect = its_parse_indirect_baser(its, baser,
							    psz, &order,
							    its->device_ids);
			break;

		case GITS_BASER_TYPE_VCPU:
			indirect = its_parse_indirect_baser(its, baser,
							    psz, &order,
							    ITS_MAX_VPEID_BITS);
			break;
		}

		err = its_setup_baser(its, baser, cache, shr, psz, order, indirect);
		if (err < 0) {
			its_free_tables(its);
			return err;
		}

		/* Update settings which will be used for next BASERn */
		psz = baser->psz;
		cache = baser->val & GITS_BASER_CACHEABILITY_MASK;
		shr = baser->val & GITS_BASER_SHAREABILITY_MASK;
	}

	return 0;
}

static int its_alloc_collections(struct its_node *its)
{
	int i;

	its->collections = kcalloc(nr_cpu_ids, sizeof(*its->collections),
				   GFP_KERNEL);
	if (!its->collections)
		return -ENOMEM;

	for (i = 0; i < nr_cpu_ids; i++)
		its->collections[i].target_address = ~0ULL;

	return 0;
}

static struct page *its_allocate_pending_table(gfp_t gfp_flags)
{
	struct page *pend_page;

	pend_page = alloc_pages(gfp_flags | __GFP_ZERO,
				get_order(LPI_PENDBASE_SZ));
	if (!pend_page)
		return NULL;

	/* Make sure the GIC will observe the zero-ed page */
	gic_flush_dcache_to_poc(page_address(pend_page), LPI_PENDBASE_SZ);

	return pend_page;
}

static void its_free_pending_table(struct page *pt)
{
	free_pages((unsigned long)page_address(pt), get_order(LPI_PENDBASE_SZ));
}

/*
 * Booting with kdump and LPIs enabled is generally fine. Any other
 * case is wrong in the absence of firmware/EFI support.
 */
static bool enabled_lpis_allowed(void)
{
	/* Allow a kdump kernel */
	if (is_kdump_kernel())
		return true;

	phys_addr_t addr;
	u64 val;

	/* Check whether the property table is in a reserved region */
	val = gicr_read_propbaser(gic_data_rdist_rd_base() + GICR_PROPBASER);
	addr = val & GENMASK_ULL(51, 12);

	return gic_check_reserved_range(addr, LPI_PROPBASE_SZ);
}

static int __init allocate_lpi_tables(void)
{
	u64 val;
	int err, cpu;

	/*
	 * If LPIs are enabled while we run this from the boot CPU,
	 * flag the RD tables as pre-allocated if the stars do align.
	 */
	val = readl_relaxed(gic_data_rdist_rd_base() + GICR_CTLR);
	if ((val & GICR_CTLR_ENABLE_LPIS) && enabled_lpis_allowed()) {
		gic_rdists->flags |= (RDIST_FLAGS_RD_TABLES_PREALLOCATED |
				      RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING);
		pr_info("GIC-2500: Using preallocated redistributor tables\n");
	}

	err = its_setup_lpi_prop_table();
	if (err)
		return err;

	/*
	 * We allocate all the pending tables anyway, as we may have a
	 * mix of RDs that have had LPIs enabled, and some that
	 * don't. We'll free the unused ones as each CPU comes online.
	 */
	for_each_possible_cpu(cpu) {
		struct page *pend_page;

		pend_page = its_allocate_pending_table(GFP_NOWAIT);
		if (!pend_page) {
			pr_err("Failed to allocate PENDBASE for CPU%d\n", cpu);
			return -ENOMEM;
		}

		gic_data_rdist_cpu(cpu)->pend_page = pend_page;
	}

	return 0;
}

static u64 its_clear_vpend_valid(void __iomem *vlpi_base)
{
	u32 count = 1000000;	/* 1s! */
	bool clean;
	u64 val;

	val = gits_read_vpendbaser(vlpi_base + GICR_VPENDBASER);
	val &= ~GICR_VPENDBASER_Valid;
	gits_write_vpendbaser(val, vlpi_base + GICR_VPENDBASER);

	do {
		val = gits_read_vpendbaser(vlpi_base + GICR_VPENDBASER);
		clean = !(val & GICR_VPENDBASER_Dirty);
		if (!clean) {
			count--;
			cpu_relax();
			udelay(1);
		}
	} while (!clean && count);

	return val;
}

static void its_cpu_init_lpis(void)
{
	void __iomem *rbase = gic_data_rdist_rd_base();
	struct page *pend_page;
	phys_addr_t paddr;
	u64 val, tmp;

	if (gic_data_rdist()->lpi_enabled)
		return;

	val = readl_relaxed(rbase + GICR_CTLR);
	if ((gic_rdists->flags & RDIST_FLAGS_RD_TABLES_PREALLOCATED) &&
	    (val & GICR_CTLR_ENABLE_LPIS)) {
		/*
		 * Check that we get the same property table on all
		 * RDs. If we don't, this is hopeless.
		 */
		paddr = gicr_read_propbaser(rbase + GICR_PROPBASER);
		paddr &= GENMASK_ULL(51, 12);
		if (WARN_ON(gic_rdists->prop_table_pa != paddr))
			add_taint(TAINT_CRAP, LOCKDEP_STILL_OK);

		paddr = gicr_read_pendbaser(rbase + GICR_PENDBASER);
		paddr &= GENMASK_ULL(51, 16);

		WARN_ON(!gic_check_reserved_range(paddr, LPI_PENDBASE_SZ));
		its_free_pending_table(gic_data_rdist()->pend_page);
		gic_data_rdist()->pend_page = NULL;

		goto out;
	}

	pend_page = gic_data_rdist()->pend_page;
	paddr = page_to_phys(pend_page);
	WARN_ON(gic_reserve_range(paddr, LPI_PENDBASE_SZ));

	/* set PROPBASE */
	val = (gic_rdists->prop_table_pa |
	       GICR_PROPBASER_InnerShareable |
	       GICR_PROPBASER_RaWaWb |
	       ((LPI_NRBITS - 1) & GICR_PROPBASER_IDBITS_MASK));

	gicr_write_propbaser(val, rbase + GICR_PROPBASER);
	tmp = gicr_read_propbaser(rbase + GICR_PROPBASER);

	if ((tmp ^ val) & GICR_PROPBASER_SHAREABILITY_MASK) {
		if (!(tmp & GICR_PROPBASER_SHAREABILITY_MASK)) {
			/*
			 * The HW reports non-shareable, we must
			 * remove the cacheability attributes as
			 * well.
			 */
			val &= ~(GICR_PROPBASER_SHAREABILITY_MASK |
				 GICR_PROPBASER_CACHEABILITY_MASK);
			val |= GICR_PROPBASER_nC;
			gicr_write_propbaser(val, rbase + GICR_PROPBASER);
		}
		pr_info_once("GIC: using cache flushing for LPI property table\n");
		gic_rdists->flags |= RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING;
	}

	/* set PENDBASE */
	val = (page_to_phys(pend_page) |
	       GICR_PENDBASER_InnerShareable |
	       GICR_PENDBASER_RaWaWb);

	gicr_write_pendbaser(val, rbase + GICR_PENDBASER);
	tmp = gicr_read_pendbaser(rbase + GICR_PENDBASER);

	if (!(tmp & GICR_PENDBASER_SHAREABILITY_MASK)) {
		/*
		 * The HW reports non-shareable, we must remove the
		 * cacheability attributes as well.
		 */
		val &= ~(GICR_PENDBASER_SHAREABILITY_MASK |
			 GICR_PENDBASER_CACHEABILITY_MASK);
		val |= GICR_PENDBASER_nC;
		gicr_write_pendbaser(val, rbase + GICR_PENDBASER);
	}

	/* Enable LPIs */
	val = readl_relaxed(rbase + GICR_CTLR);
	val |= GICR_CTLR_ENABLE_LPIS;
	writel_relaxed(val, rbase + GICR_CTLR);

	if (gic_rdists->has_vlpis) {
		void __iomem *vlpi_base = gic_data_rdist_vlpi_base();

		/*
		 * It's possible for CPU to receive VLPIs before it is
		 * sheduled as a vPE, especially for the first CPU, and the
		 * VLPI with INTID larger than 2^(IDbits+1) will be considered
		 * as out of range and dropped by GIC.
		 * So we initialize IDbits to known value to avoid VLPI drop.
		 */
		val = (LPI_NRBITS - 1) & GICR_VPROPBASER_IDBITS_MASK;
		pr_debug("GICv4: CPU%d: Init IDbits to 0x%llx for GICR_VPROPBASER\n",
			smp_processor_id(), val);
		gits_write_vpropbaser(val, vlpi_base + GICR_VPROPBASER);

		/*
		 * Also clear Valid bit of GICR_VPENDBASER, in case some
		 * ancient programming gets left in and has possibility of
		 * corrupting memory.
		 */
		val = its_clear_vpend_valid(vlpi_base);
		WARN_ON(val & GICR_VPENDBASER_Dirty);
	}

	/* Make sure the GIC has seen the above */
	dsb(sy);
out:
	gic_data_rdist()->lpi_enabled = true;
	pr_info("GIC-2500: CPU%d: using %s LPI pending table @%pa\n",
		smp_processor_id(),
		gic_data_rdist()->pend_page ? "allocated" : "reserved",
		&paddr);
}

static void its_cpu_init_collection(struct its_node *its)
{
	int cpu = smp_processor_id();
	u64 target;

	/* avoid cross node collections and its mapping */
	if (its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_23144) {
		struct device_node *cpu_node;

		cpu_node = of_get_cpu_node(cpu, NULL);
		if (its->numa_node != NUMA_NO_NODE &&
			its->numa_node != of_node_to_nid(cpu_node))
			return;
	}

	/*
	 * We now have to bind each collection to its target
	 * redistributor.
	 */
	if (gic_read_typer(its->base + GITS_TYPER) & GITS_TYPER_PTA) {
		/*
		 * This ITS wants the physical address of the
		 * redistributor.
		 */
		target = gic_data_rdist()->phys_base;
	} else {
		/* This ITS wants a linear CPU number. */
		target = gic_read_typer(gic_data_rdist_rd_base() + GICR_TYPER);
		target = GICR_TYPER_CPU_NUMBER(target) << 16;
	}

	/* Perform collection mapping */
	its->collections[cpu].target_address = target;
	its->collections[cpu].col_id = cpu % 64;

	its_send_mapc(its, &its->collections[cpu], 1);
	its_send_invall(its, &its->collections[cpu]);
}

static void its_cpu_init_collections(void)
{
	struct its_node *its;

	raw_spin_lock(&its_lock);

	list_for_each_entry(its, &its_nodes, entry)
		its_cpu_init_collection(its);

	raw_spin_unlock(&its_lock);
}

static struct its_device *its_find_device(struct its_node *its, u32 dev_id)
{
	struct its_device *its_dev = NULL, *tmp;
	unsigned long flags;

	raw_spin_lock_irqsave(&its->lock, flags);

	list_for_each_entry(tmp, &its->its_device_list, entry) {
		if (tmp->device_id == dev_id) {
			its_dev = tmp;
			break;
		}
	}

	raw_spin_unlock_irqrestore(&its->lock, flags);

	return its_dev;
}

static struct its_baser *its_get_baser(struct its_node *its, u32 type)
{
	int i;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		if (GITS_BASER_TYPE(its->tables[i].val) == type)
			return &its->tables[i];
	}

	return NULL;
}

static bool its_alloc_table_entry(struct its_node *its,
				  struct its_baser *baser, u32 id)
{
	struct page *page;
	u32 esz, idx;
	__le64 *table;

	/* Don't allow device id that exceeds single, flat table limit */
	esz = GITS_BASER_ENTRY_SIZE(baser->val);
	if (!(baser->val & GITS_BASER_INDIRECT))
		return (id < (PAGE_ORDER_TO_SIZE(baser->order) / esz));

	/* Compute 1st level table index & check if that exceeds table limit */
	idx = id >> ilog2(baser->psz / esz);
	if (idx >= (PAGE_ORDER_TO_SIZE(baser->order) / GITS_LVL1_ENTRY_SIZE))
		return false;

	table = baser->base;

	/* Allocate memory for 2nd level table */
	if (!table[idx]) {
		page = alloc_pages_node(its->numa_node, GFP_KERNEL | __GFP_ZERO,
					get_order(baser->psz));
		if (!page)
			return false;

		/* Flush Lvl2 table to PoC if hw doesn't support coherency */
		if (!(baser->val & GITS_BASER_SHAREABILITY_MASK))
			gic_flush_dcache_to_poc(page_address(page), baser->psz);

		table[idx] = cpu_to_le64(page_to_phys(page) | GITS_BASER_VALID);

		/* Flush Lvl1 entry to PoC if hw doesn't support coherency */
		if (!(baser->val & GITS_BASER_SHAREABILITY_MASK))
			gic_flush_dcache_to_poc(table + idx, GITS_LVL1_ENTRY_SIZE);

		/* Ensure updated table contents are visible to ITS hardware */
		dsb(sy);
	}

	return true;
}

static bool its_alloc_device_table(struct its_node *its, u32 dev_id)
{
	struct its_baser *baser;

	baser = its_get_baser(its, GITS_BASER_TYPE_DEVICE);

	/* Don't allow device id that exceeds ITS hardware limit */
	if (!baser)
		return (ilog2(dev_id) < its->device_ids);

	return its_alloc_table_entry(its, baser, dev_id);
}

static bool its_alloc_vpe_table(u32 vpe_id)
{
	struct its_node *its;

	/*
	 * Make sure the L2 tables are allocated on *all* v4 ITSs. We
	 * could try and only do it on ITSs corresponding to devices
	 * that have interrupts targeted at this VPE, but the
	 * complexity becomes crazy (and you have tons of memory
	 * anyway, right?).
	 */
	list_for_each_entry(its, &its_nodes, entry) {
		struct its_baser *baser;

		if (!its->is_v4)
			continue;

		baser = its_get_baser(its, GITS_BASER_TYPE_VCPU);
		if (!baser)
			return false;

		if (!its_alloc_table_entry(its, baser, vpe_id))
			return false;
	}

	return true;
}

static struct its_device *its_create_device(struct its_node *its, u32 dev_id,
					    int nvecs, bool alloc_lpis)
{
	struct its_device *dev;
	unsigned long *lpi_map = NULL;
	unsigned long flags;
	u16 *col_map = NULL;
	void *itt;
	int lpi_base;
	int nr_lpis;
	int nr_ites;
	int sz;

	if (!its_alloc_device_table(its, dev_id))
		return NULL;

	if (WARN_ON(!is_power_of_2(nvecs)))
		nvecs = roundup_pow_of_two(nvecs);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	/*
	 * Even if the device wants a single LPI, the ITT must be
	 * sized as a power of two (and you need at least one bit...).
	 */
	nr_ites = max(2, nvecs);
	sz = nr_ites * its->ite_size;
	sz = max(sz, ITS_ITT_ALIGN) + ITS_ITT_ALIGN - 1;
	itt = kzalloc_node(sz, GFP_KERNEL, its->numa_node);
	if (alloc_lpis) {
		lpi_map = its_lpi_alloc(nvecs, &lpi_base, &nr_lpis);
		if (lpi_map)
			col_map = kcalloc(nr_lpis, sizeof(*col_map),
					  GFP_KERNEL);
	} else {
		col_map = kcalloc(nr_ites, sizeof(*col_map), GFP_KERNEL);
		nr_lpis = 0;
		lpi_base = 0;
	}

	if (!dev || !itt ||  !col_map || (!lpi_map && alloc_lpis)) {
		kfree(dev);
		kfree(itt);
		kfree(lpi_map);
		kfree(col_map);
		return NULL;
	}

	gic_flush_dcache_to_poc(itt, sz);

	dev->its = its;
	dev->itt = itt;
	dev->nr_ites = nr_ites;
	dev->event_map.lpi_map = lpi_map;
	dev->event_map.col_map = col_map;
	dev->event_map.lpi_base = lpi_base;
	dev->event_map.nr_lpis = nr_lpis;
	mutex_init(&dev->event_map.vlpi_lock);
	dev->device_id = dev_id;
	INIT_LIST_HEAD(&dev->entry);

	raw_spin_lock_irqsave(&its->lock, flags);
	list_add(&dev->entry, &its->its_device_list);
	raw_spin_unlock_irqrestore(&its->lock, flags);

	/* Map device to its ITT */
	its_send_mapd(dev, 1);

	return dev;
}

static void its_free_device(struct its_device *its_dev)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&its_dev->its->lock, flags);
	list_del(&its_dev->entry);
	raw_spin_unlock_irqrestore(&its_dev->its->lock, flags);
	kfree(its_dev->itt);
	kfree(its_dev);
}

static int its_alloc_device_irq(struct its_device *dev, int nvecs, irq_hw_number_t *hwirq)
{
	int idx;

	/* Find a free LPI region in lpi_map and allocate them. */
	idx = bitmap_find_free_region(dev->event_map.lpi_map,
				      dev->event_map.nr_lpis,
				      get_count_order(nvecs));
	if (idx < 0)
		return -ENOSPC;

	*hwirq = dev->event_map.lpi_base + idx;

	return 0;
}

static int its_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *info)
{
	struct its_node *its;
	struct its_device *its_dev;
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int err = 0;

	/*
	 * We ignore "dev" entirely, and rely on the dev_id that has
	 * been passed via the scratchpad. This limits this domain's
	 * usefulness to upper layers that definitely know that they
	 * are built on top of the ITS.
	 */
	dev_id = info->scratchpad[0].ul;

	msi_info = msi_get_domain_info(domain);
	its = msi_info->data;

	if (!gic_rdists->has_direct_lpi &&
	    vpe_proxy.dev &&
	    vpe_proxy.dev->its == its &&
	    dev_id == vpe_proxy.dev->device_id) {
		/* Bad luck. Get yourself a better implementation */
		WARN_ONCE(1, "DevId %x clashes with GICv4 VPE proxy device\n",
			  dev_id);
		return -EINVAL;
	}

	mutex_lock(&its->dev_alloc_lock);
	its_dev = its_find_device(its, dev_id);
	if (its_dev) {
		/*
		 * We already have seen this ID, probably through
		 * another alias (PCI bridge of some sort). No need to
		 * create the device.
		 */
		its_dev->shared = true;
		pr_debug("Reusing ITT for devID %x\n", dev_id);
		goto out;
	}

	its_dev = its_create_device(its, dev_id, nvec, true);
	if (!its_dev) {
		err = -ENOMEM;
		goto out;
	}

	pr_debug("ITT %d entries, %d bits\n", nvec, ilog2(nvec));
out:
	mutex_unlock(&its->dev_alloc_lock);
	info->scratchpad[0].ptr = its_dev;
	return err;
}

static struct msi_domain_ops its_msi_domain_ops = {
	.msi_prepare	= its_msi_prepare,
};

static int its_irq_gic_domain_alloc(struct irq_domain *domain,
				    unsigned int virq,
				    irq_hw_number_t hwirq)
{
	struct irq_fwspec fwspec;

	if (irq_domain_get_of_node(domain->parent)) {
		fwspec.fwnode = domain->parent->fwnode;
		fwspec.param_count = 3;
		fwspec.param[0] = GIC_IRQ_TYPE_LPI;
		fwspec.param[1] = hwirq;
		fwspec.param[2] = IRQ_TYPE_EDGE_RISING;
	} else if (is_fwnode_irqchip(domain->parent->fwnode)) {
		fwspec.fwnode = domain->parent->fwnode;
		fwspec.param_count = 2;
		fwspec.param[0] = hwirq;
		fwspec.param[1] = IRQ_TYPE_EDGE_RISING;
	} else {
		return -EINVAL;
	}

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
}

static int its_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *args)
{
	msi_alloc_info_t *info = args;
	struct its_device *its_dev = info->scratchpad[0].ptr;
	struct its_node *its = its_dev->its;
	irq_hw_number_t hwirq;
	int err;
	int i;

	err = its_alloc_device_irq(its_dev, nr_irqs, &hwirq);
	if (err)
		return err;

	err = iommu_dma_prepare_msi(info->desc, its->get_msi_base(its_dev));
	if (err)
		return err;

	for (i = 0; i < nr_irqs; i++) {
		err = its_irq_gic_domain_alloc(domain, virq + i, hwirq + i);
		if (err)
			return err;

		irq_domain_set_hwirq_and_chip(domain, virq + i,
					      hwirq + i, &its_irq_chip, its_dev);
		irqd_set_single_target(irq_desc_get_irq_data(irq_to_desc(virq + i)));
		pr_debug("ID:%d pID:%d vID:%d\n",
			 (int)(hwirq + i - its_dev->event_map.lpi_base),
			 (int)(hwirq + i), virq + i);
	}

	return 0;
}

static int its_cpumask_first(struct its_device *its_dev,
                            const struct cpumask *cpu_mask)
{
    unsigned int skt, skt_id, i;
    phys_addr_t its_phys_base;
    unsigned int cpu, cpus = 0;

    unsigned int skt_cpu_cnt[MAX_MARS3_SKT_COUNT] = {0};

    for (i = 0; i < nr_cpu_ids; i++) {
        skt = (cpu_logical_map(i) >> 16) & 0xff;
        if ((skt >= 0) && (skt < MAX_MARS3_SKT_COUNT)) {
            skt_cpu_cnt[skt]++;
        } else if (0xff != skt ) {
            pr_err("socket address: %d is out of range.", skt);
        }
    }

    its_phys_base = its_dev->its->phys_base;
    skt_id = (its_phys_base >> 41) & 0x7;

    if (0 != skt_id) {
        for (i = 0; i < skt_id; i++) {
            cpus += skt_cpu_cnt[i];
        }
    }

    cpu = cpumask_first(cpu_mask);
    if ((cpu > cpus) && (cpu < (cpus + skt_cpu_cnt[skt_id]))) {
        cpus = cpu;
    }

    if (is_kdump_kernel()) {
         skt = (cpu_logical_map(cpu) >> 16) & 0xff;

         if (skt_id == skt)
              return cpu;

         for (i = 0; i < nr_cpu_ids; i++) {
              skt = (cpu_logical_map(i) >> 16) & 0xff;

              if ((skt >= 0) && (skt < MAX_MARS3_SKT_COUNT)) {
                   if (skt_id == skt)
                        return i;
              } else if (0xff != skt) {
                   pr_err("socket address: %d is out of range.", skt);
              }
         }
    }

    return cpus;
}

static int its_irq_domain_activate(struct irq_domain *domain,
				   struct irq_data *d, bool reserve)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);
	const struct cpumask *cpu_mask = cpu_online_mask;
	int cpu;

	/* get the cpu_mask of local node */
	if (its_dev->its->numa_node >= 0)
		cpu_mask = cpumask_of_node(its_dev->its->numa_node);
	cpu = its_cpumask_first(its_dev, cpu_mask);
	if (cpu >= nr_cpu_ids) {
		if (its_dev->its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_23144)
			return -EINVAL;

		cpu = cpumask_first(cpu_online_mask);
	}

	its_dev->event_map.col_map[event] = cpu;
	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	/* Map the GIC IRQ and event to the device */
	its_send_mapti(its_dev, d->hwirq, event);
	return 0;
}

static void its_irq_domain_deactivate(struct irq_domain *domain,
				      struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);

	/* Stop the delivery of interrupts */
	its_send_discard(its_dev, event);
}

static void its_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_node *its = its_dev->its;
	int i;

	bitmap_release_region(its_dev->event_map.lpi_map,
			      its_get_event_id(irq_domain_get_irq_data(domain, virq)),
			      get_count_order(nr_irqs));

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *data = irq_domain_get_irq_data(domain,
								virq + i);
		/* Nuke the entry in the domain */
		irq_domain_reset_irq_data(data);
	}

	mutex_lock(&its->dev_alloc_lock);

	/*
	 * If all interrupts have been freed, start mopping the
	 * floor. This is conditionned on the device not being shared.
	 */
	if (!its_dev->shared &&
	    bitmap_empty(its_dev->event_map.lpi_map,
			 its_dev->event_map.nr_lpis)) {
		its_lpi_free(its_dev->event_map.lpi_map,
			     its_dev->event_map.lpi_base,
			     its_dev->event_map.nr_lpis);
		kfree(its_dev->event_map.col_map);

		/* Unmap device/itt */
		its_send_mapd(its_dev, 0);
		its_free_device(its_dev);
	}

	mutex_unlock(&its->dev_alloc_lock);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops its_domain_ops = {
	.alloc			= its_irq_domain_alloc,
	.free			= its_irq_domain_free,
	.activate		= its_irq_domain_activate,
	.deactivate		= its_irq_domain_deactivate,
};

/*
 * This is insane.
 *
 * If a GICv4 doesn't implement Direct LPIs (which is extremely
 * likely), the only way to perform an invalidate is to use a fake
 * device to issue an INV command, implying that the LPI has first
 * been mapped to some event on that device. Since this is not exactly
 * cheap, we try to keep that mapping around as long as possible, and
 * only issue an UNMAP if we're short on available slots.
 *
 * Broken by design(tm).
 */
static void its_vpe_db_proxy_unmap_locked(struct its_vpe *vpe)
{
	/* Already unmapped? */
	if (vpe->vpe_proxy_event == -1)
		return;

	its_send_discard(vpe_proxy.dev, vpe->vpe_proxy_event);
	vpe_proxy.vpes[vpe->vpe_proxy_event] = NULL;

	/*
	 * We don't track empty slots at all, so let's move the
	 * next_victim pointer if we can quickly reuse that slot
	 * instead of nuking an existing entry. Not clear that this is
	 * always a win though, and this might just generate a ripple
	 * effect... Let's just hope VPEs don't migrate too often.
	 */
	if (vpe_proxy.vpes[vpe_proxy.next_victim])
		vpe_proxy.next_victim = vpe->vpe_proxy_event;

	vpe->vpe_proxy_event = -1;
}

static void its_vpe_db_proxy_unmap(struct its_vpe *vpe)
{
	if (!gic_rdists->has_direct_lpi) {
		unsigned long flags;

		raw_spin_lock_irqsave(&vpe_proxy.lock, flags);
		its_vpe_db_proxy_unmap_locked(vpe);
		raw_spin_unlock_irqrestore(&vpe_proxy.lock, flags);
	}
}

static void its_vpe_db_proxy_map_locked(struct its_vpe *vpe)
{
	/* Already mapped? */
	if (vpe->vpe_proxy_event != -1)
		return;

	/* This slot was already allocated. Kick the other VPE out. */
	if (vpe_proxy.vpes[vpe_proxy.next_victim])
		its_vpe_db_proxy_unmap_locked(vpe_proxy.vpes[vpe_proxy.next_victim]);

	/* Map the new VPE instead */
	vpe_proxy.vpes[vpe_proxy.next_victim] = vpe;
	vpe->vpe_proxy_event = vpe_proxy.next_victim;
	vpe_proxy.next_victim = (vpe_proxy.next_victim + 1) % vpe_proxy.dev->nr_ites;

	vpe_proxy.dev->event_map.col_map[vpe->vpe_proxy_event] = vpe->col_idx;
	its_send_mapti(vpe_proxy.dev, vpe->vpe_db_lpi, vpe->vpe_proxy_event);
}

static void its_vpe_db_proxy_move(struct its_vpe *vpe, int from, int to)
{
	unsigned long flags;
	struct its_collection *target_col;

	if (gic_rdists->has_direct_lpi) {
		void __iomem *rdbase;

		rdbase = per_cpu_ptr(gic_rdists->rdist, from)->rd_base;
		gic_write_lpir(vpe->vpe_db_lpi, rdbase + GICR_CLRLPIR);
		while (gic_read_lpir(rdbase + GICR_SYNCR) & 1)
			cpu_relax();

		return;
	}

	raw_spin_lock_irqsave(&vpe_proxy.lock, flags);

	its_vpe_db_proxy_map_locked(vpe);

	target_col = &vpe_proxy.dev->its->collections[to];
	its_send_movi(vpe_proxy.dev, target_col, vpe->vpe_proxy_event);
	vpe_proxy.dev->event_map.col_map[vpe->vpe_proxy_event] = to;

	raw_spin_unlock_irqrestore(&vpe_proxy.lock, flags);
}

static int its_vpe_set_affinity(struct irq_data *d,
				const struct cpumask *mask_val,
				bool force)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);
	int cpu = cpumask_first(mask_val);

	/*
	 * Changing affinity is mega expensive, so let's be as lazy as
	 * we can and only do it if we really have to. Also, if mapped
	 * into the proxy device, we need to move the doorbell
	 * interrupt to its new location.
	 */
	if (vpe->col_idx != cpu) {
		int from = vpe->col_idx;

		vpe->col_idx = cpu;
		its_send_vmovp(vpe);
		its_vpe_db_proxy_move(vpe, from, cpu);
	}

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}

static void its_vpe_schedule(struct its_vpe *vpe)
{
	void __iomem *vlpi_base = gic_data_rdist_vlpi_base();
	u64 val;

	/* Schedule the VPE */
	val  = virt_to_phys(page_address(vpe->its_vm->vprop_page)) &
		GENMASK_ULL(51, 12);
	val |= (LPI_NRBITS - 1) & GICR_VPROPBASER_IDBITS_MASK;
	val |= GICR_VPROPBASER_RaWb;
	val |= GICR_VPROPBASER_InnerShareable;
	gits_write_vpropbaser(val, vlpi_base + GICR_VPROPBASER);

	val  = virt_to_phys(page_address(vpe->vpt_page)) &
		GENMASK_ULL(51, 16);
	val |= GICR_VPENDBASER_RaWaWb;
	val |= GICR_VPENDBASER_NonShareable;
	/*
	 * There is no good way of finding out if the pending table is
	 * empty as we can race against the doorbell interrupt very
	 * easily. So in the end, vpe->pending_last is only an
	 * indication that the vcpu has something pending, not one
	 * that the pending table is empty. A good implementation
	 * would be able to read its coarse map pretty quickly anyway,
	 * making this a tolerable issue.
	 */
	val |= GICR_VPENDBASER_PendingLast;
	val |= vpe->idai ? GICR_VPENDBASER_IDAI : 0;
	val |= GICR_VPENDBASER_Valid;
	gits_write_vpendbaser(val, vlpi_base + GICR_VPENDBASER);
}

static void its_vpe_deschedule(struct its_vpe *vpe)
{
	void __iomem *vlpi_base = gic_data_rdist_vlpi_base();
	u64 val;

	val = its_clear_vpend_valid(vlpi_base);

	if (unlikely(val & GICR_VPENDBASER_Dirty)) {
		pr_err_ratelimited("ITS virtual pending table not cleaning\n");
		vpe->idai = false;
		vpe->pending_last = true;
	} else {
		vpe->idai = !!(val & GICR_VPENDBASER_IDAI);
		vpe->pending_last = !!(val & GICR_VPENDBASER_PendingLast);
	}
}

static void its_vpe_invall(struct its_vpe *vpe)
{
	struct its_node *its;

	list_for_each_entry(its, &its_nodes, entry) {
		if (!its->is_v4)
			continue;

		if (its_list_map && !vpe->its_vm->vlpi_count[its->list_nr])
			continue;

		/*
		 * Sending a VINVALL to a single ITS is enough, as all
		 * we need is to reach the redistributors.
		 */
		its_send_vinvall(its, vpe);
		return;
	}
}

static int its_vpe_set_vcpu_affinity(struct irq_data *d, void *vcpu_info)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);
	struct its_cmd_info *info = vcpu_info;

	switch (info->cmd_type) {
	case SCHEDULE_VPE:
		its_vpe_schedule(vpe);
		return 0;

	case DESCHEDULE_VPE:
		its_vpe_deschedule(vpe);
		return 0;

	case INVALL_VPE:
		its_vpe_invall(vpe);
		return 0;

	default:
		return -EINVAL;
	}
}

static void its_vpe_send_cmd(struct its_vpe *vpe,
			     void (*cmd)(struct its_device *, u32))
{
	unsigned long flags;

	raw_spin_lock_irqsave(&vpe_proxy.lock, flags);

	its_vpe_db_proxy_map_locked(vpe);
	cmd(vpe_proxy.dev, vpe->vpe_proxy_event);

	raw_spin_unlock_irqrestore(&vpe_proxy.lock, flags);
}

static void its_vpe_send_inv(struct irq_data *d)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);

	if (gic_rdists->has_direct_lpi) {
		void __iomem *rdbase;

		rdbase = per_cpu_ptr(gic_rdists->rdist, vpe->col_idx)->rd_base;
		gic_write_lpir(vpe->vpe_db_lpi, rdbase + GICR_INVLPIR);
		while (gic_read_lpir(rdbase + GICR_SYNCR) & 1)
			cpu_relax();
	} else {
		its_vpe_send_cmd(vpe, its_send_inv);
	}
}

static void its_vpe_mask_irq(struct irq_data *d)
{
	/*
	 * We need to unmask the LPI, which is described by the parent
	 * irq_data. Instead of calling into the parent (which won't
	 * exactly do the right thing, let's simply use the
	 * parent_data pointer. Yes, I'm naughty.
	 */
	lpi_write_config(d->parent_data, LPI_PROP_ENABLED, 0);
	its_vpe_send_inv(d);
}

static void its_vpe_unmask_irq(struct irq_data *d)
{
	/* Same hack as above... */
	lpi_write_config(d->parent_data, 0, LPI_PROP_ENABLED);
	its_vpe_send_inv(d);
}

static int its_vpe_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which,
				     bool state)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	if (gic_rdists->has_direct_lpi) {
		void __iomem *rdbase;

		rdbase = per_cpu_ptr(gic_rdists->rdist, vpe->col_idx)->rd_base;
		if (state) {
			gic_write_lpir(vpe->vpe_db_lpi, rdbase + GICR_SETLPIR);
		} else {
			gic_write_lpir(vpe->vpe_db_lpi, rdbase + GICR_CLRLPIR);
			while (gic_read_lpir(rdbase + GICR_SYNCR) & 1)
				cpu_relax();
		}
	} else {
		if (state)
			its_vpe_send_cmd(vpe, its_send_int);
		else
			its_vpe_send_cmd(vpe, its_send_clear);
	}

	return 0;
}

static struct irq_chip its_vpe_irq_chip = {
	.name			= "GICv4-vpe",
	.irq_mask		= its_vpe_mask_irq,
	.irq_unmask		= its_vpe_unmask_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= its_vpe_set_affinity,
	.irq_set_irqchip_state	= its_vpe_set_irqchip_state,
	.irq_set_vcpu_affinity	= its_vpe_set_vcpu_affinity,
};

static int its_vpe_id_alloc(void)
{
	return ida_simple_get(&its_vpeid_ida, 0, ITS_MAX_VPEID, GFP_KERNEL);
}

static void its_vpe_id_free(u16 id)
{
	ida_simple_remove(&its_vpeid_ida, id);
}

static int its_vpe_init(struct its_vpe *vpe)
{
	struct page *vpt_page;
	int vpe_id;

	/* Allocate vpe_id */
	vpe_id = its_vpe_id_alloc();
	if (vpe_id < 0)
		return vpe_id;

	/* Allocate VPT */
	vpt_page = its_allocate_pending_table(GFP_KERNEL);
	if (!vpt_page) {
		its_vpe_id_free(vpe_id);
		return -ENOMEM;
	}

	if (!its_alloc_vpe_table(vpe_id)) {
		its_vpe_id_free(vpe_id);
		its_free_pending_table(vpt_page);
		return -ENOMEM;
	}

	vpe->vpe_id = vpe_id;
	vpe->vpt_page = vpt_page;
	vpe->vpe_proxy_event = -1;

	return 0;
}

static void its_vpe_teardown(struct its_vpe *vpe)
{
	its_vpe_db_proxy_unmap(vpe);
	its_vpe_id_free(vpe->vpe_id);
	its_free_pending_table(vpe->vpt_page);
}

static void its_vpe_irq_domain_free(struct irq_domain *domain,
				    unsigned int virq,
				    unsigned int nr_irqs)
{
	struct its_vm *vm = domain->host_data;
	int i;

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *data = irq_domain_get_irq_data(domain,
								virq + i);
		struct its_vpe *vpe = irq_data_get_irq_chip_data(data);

		BUG_ON(vm != vpe->its_vm);

		clear_bit(data->hwirq, vm->db_bitmap);
		its_vpe_teardown(vpe);
		irq_domain_reset_irq_data(data);
	}

	if (bitmap_empty(vm->db_bitmap, vm->nr_db_lpis)) {
		its_lpi_free(vm->db_bitmap, vm->db_lpi_base, vm->nr_db_lpis);
		its_free_prop_table(vm->vprop_page);
	}
}

static int its_vpe_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs, void *args)
{
	struct its_vm *vm = args;
	unsigned long *bitmap;
	struct page *vprop_page;
	int base, nr_ids, i, err = 0;

	BUG_ON(!vm);

	bitmap = its_lpi_alloc(roundup_pow_of_two(nr_irqs), &base, &nr_ids);
	if (!bitmap)
		return -ENOMEM;

	if (nr_ids < nr_irqs) {
		its_lpi_free(bitmap, base, nr_ids);
		return -ENOMEM;
	}

	vprop_page = its_allocate_prop_table(GFP_KERNEL);
	if (!vprop_page) {
		its_lpi_free(bitmap, base, nr_ids);
		return -ENOMEM;
	}

	vm->db_bitmap = bitmap;
	vm->db_lpi_base = base;
	vm->nr_db_lpis = nr_ids;
	vm->vprop_page = vprop_page;

	for (i = 0; i < nr_irqs; i++) {
		vm->vpes[i]->vpe_db_lpi = base + i;
		err = its_vpe_init(vm->vpes[i]);
		if (err)
			break;
		err = its_irq_gic_domain_alloc(domain, virq + i,
					       vm->vpes[i]->vpe_db_lpi);
		if (err)
			break;
		irq_domain_set_hwirq_and_chip(domain, virq + i, i,
					      &its_vpe_irq_chip, vm->vpes[i]);
		set_bit(i, bitmap);
	}

	if (err) {
		if (i > 0)
			its_vpe_irq_domain_free(domain, virq, i - 1);

		its_lpi_free(bitmap, base, nr_ids);
		its_free_prop_table(vprop_page);
	}

	return err;
}

static int its_vpe_irq_domain_activate(struct irq_domain *domain,
				       struct irq_data *d, bool reserve)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);
	struct its_node *its;

	/* If we use the list map, we issue VMAPP on demand... */
	if (its_list_map)
		return 0;

	/* Map the VPE to the first possible CPU */
	vpe->col_idx = cpumask_first(cpu_online_mask);

	list_for_each_entry(its, &its_nodes, entry) {
		if (!its->is_v4)
			continue;

		its_send_vmapp(its, vpe, true);
		its_send_vinvall(its, vpe);
	}

	irq_data_update_effective_affinity(d, cpumask_of(vpe->col_idx));

	return 0;
}

static void its_vpe_irq_domain_deactivate(struct irq_domain *domain,
					  struct irq_data *d)
{
	struct its_vpe *vpe = irq_data_get_irq_chip_data(d);
	struct its_node *its;

	/*
	 * If we use the list map, we unmap the VPE once no VLPIs are
	 * associated with the VM.
	 */
	if (its_list_map)
		return;

	list_for_each_entry(its, &its_nodes, entry) {
		if (!its->is_v4)
			continue;

		its_send_vmapp(its, vpe, false);
	}
}

static const struct irq_domain_ops its_vpe_domain_ops = {
	.alloc			= its_vpe_irq_domain_alloc,
	.free			= its_vpe_irq_domain_free,
	.activate		= its_vpe_irq_domain_activate,
	.deactivate		= its_vpe_irq_domain_deactivate,
};

static int its_force_quiescent(void __iomem *base)
{
	u32 count = 1000000;	/* 1s */
	u32 val;

	val = readl_relaxed(base + GITS_CTLR);
	/*
	 * GIC architecture specification requires the ITS to be both
	 * disabled and quiescent for writes to GITS_BASER<n> or
	 * GITS_CBASER to not have UNPREDICTABLE results.
	 */
	if ((val & GITS_CTLR_QUIESCENT) && !(val & GITS_CTLR_ENABLE))
		return 0;

	/* Disable the generation of all interrupts to this ITS */
	val &= ~(GITS_CTLR_ENABLE | GITS_CTLR_ImDe);
	writel_relaxed(val, base + GITS_CTLR);

	/* Poll GITS_CTLR and wait until ITS becomes quiescent */
	while (1) {
		val = readl_relaxed(base + GITS_CTLR);
		if (val & GITS_CTLR_QUIESCENT)
			return 0;

		count--;
		if (!count)
			return -EBUSY;

		cpu_relax();
		udelay(1);
	}
}

static bool __maybe_unused its_enable_quirk_cavium_22375(void *data)
{
	struct its_node *its = data;

	/* erratum 22375: only alloc 8MB table size */
	its->device_ids = 0x14;		/* 20 bits, 8MB */
	its->flags |= ITS_FLAGS_WORKAROUND_CAVIUM_22375;

	return true;
}

static bool __maybe_unused its_enable_quirk_cavium_23144(void *data)
{
	struct its_node *its = data;

	its->flags |= ITS_FLAGS_WORKAROUND_CAVIUM_23144;

	return true;
}

static bool __maybe_unused its_enable_quirk_qdf2400_e0065(void *data)
{
	struct its_node *its = data;

	/* On QDF2400, the size of the ITE is 16Bytes */
	its->ite_size = 16;

	return true;
}

static u64 its_irq_get_msi_base_pre_its(struct its_device *its_dev)
{
	struct its_node *its = its_dev->its;

	/*
	 * The Socionext Synquacer SoC has a so-called 'pre-ITS',
	 * which maps 32-bit writes targeted at a separate window of
	 * size '4 << device_id_bits' onto writes to GITS_TRANSLATER
	 * with device ID taken from bits [device_id_bits + 1:2] of
	 * the window offset.
	 */
	return its->pre_its_base + (its_dev->device_id << 2);
}

static bool __maybe_unused its_enable_quirk_socionext_synquacer(void *data)
{
	struct its_node *its = data;
	u32 pre_its_window[2];
	u32 ids;

	if (!fwnode_property_read_u32_array(its->fwnode_handle,
					   "socionext,synquacer-pre-its",
					   pre_its_window,
					   ARRAY_SIZE(pre_its_window))) {

		its->pre_its_base = pre_its_window[0];
		its->get_msi_base = its_irq_get_msi_base_pre_its;

		ids = ilog2(pre_its_window[1]) - 2;
		if (its->device_ids > ids)
			its->device_ids = ids;

		/* the pre-ITS breaks isolation, so disable MSI remapping */
		its->msi_domain_flags &= ~IRQ_DOMAIN_FLAG_MSI_REMAP;
		return true;
	}
	return false;
}

static bool __maybe_unused its_enable_quirk_hip07_161600802(void *data)
{
	struct its_node *its = data;

	/*
	 * Hip07 insists on using the wrong address for the VLPI
	 * page. Trick it into doing the right thing...
	 */
	its->vlpi_redist_offset = SZ_128K;
	return true;
}

static const struct gic_quirk its_quirks[] = {
#ifdef CONFIG_CAVIUM_ERRATUM_22375
	{
		.desc	= "ITS: Cavium errata 22375, 24313",
		.iidr	= 0xa100034c,	/* ThunderX pass 1.x */
		.mask	= 0xffff0fff,
		.init	= its_enable_quirk_cavium_22375,
	},
#endif
#ifdef CONFIG_CAVIUM_ERRATUM_23144
	{
		.desc	= "ITS: Cavium erratum 23144",
		.iidr	= 0xa100034c,	/* ThunderX pass 1.x */
		.mask	= 0xffff0fff,
		.init	= its_enable_quirk_cavium_23144,
	},
#endif
#ifdef CONFIG_QCOM_QDF2400_ERRATUM_0065
	{
		.desc	= "ITS: QDF2400 erratum 0065",
		.iidr	= 0x00001070, /* QDF2400 ITS rev 1.x */
		.mask	= 0xffffffff,
		.init	= its_enable_quirk_qdf2400_e0065,
	},
#endif
#ifdef CONFIG_SOCIONEXT_SYNQUACER_PREITS
	{
		/*
		 * The Socionext Synquacer SoC incorporates ARM's own GIC-500
		 * implementation, but with a 'pre-ITS' added that requires
		 * special handling in software.
		 */
		.desc	= "ITS: Socionext Synquacer pre-ITS",
		.iidr	= 0x0001143b,
		.mask	= 0xffffffff,
		.init	= its_enable_quirk_socionext_synquacer,
	},
#endif
#ifdef CONFIG_HISILICON_ERRATUM_161600802
	{
		.desc	= "ITS: Hip07 erratum 161600802",
		.iidr	= 0x00000004,
		.mask	= 0xffffffff,
		.init	= its_enable_quirk_hip07_161600802,
	},
#endif
	{
	}
};

static void its_enable_quirks(struct its_node *its)
{
	u32 iidr = readl_relaxed(its->base + GITS_IIDR);

	gic_enable_quirks(iidr, its_quirks, its);
}

static int its_save_disable(void)
{
	struct its_node *its;
	int err = 0;

	raw_spin_lock(&its_lock);
	list_for_each_entry(its, &its_nodes, entry) {
		void __iomem *base;

		if (!(its->flags & ITS_FLAGS_SAVE_SUSPEND_STATE))
			continue;

		base = its->base;
		its->ctlr_save = readl_relaxed(base + GITS_CTLR);
		err = its_force_quiescent(base);
		if (err) {
			pr_err("ITS@%pa: failed to quiesce: %d\n",
			       &its->phys_base, err);
			writel_relaxed(its->ctlr_save, base + GITS_CTLR);
			goto err;
		}

		its->cbaser_save = gits_read_cbaser(base + GITS_CBASER);
	}

err:
	if (err) {
		list_for_each_entry_continue_reverse(its, &its_nodes, entry) {
			void __iomem *base;

			if (!(its->flags & ITS_FLAGS_SAVE_SUSPEND_STATE))
				continue;

			base = its->base;
			writel_relaxed(its->ctlr_save, base + GITS_CTLR);
		}
	}
	raw_spin_unlock(&its_lock);

	return err;
}

static void its_restore_enable(void)
{
	struct its_node *its;
	int ret;

	raw_spin_lock(&its_lock);
	list_for_each_entry(its, &its_nodes, entry) {
		void __iomem *base;
		int i;

		if (!(its->flags & ITS_FLAGS_SAVE_SUSPEND_STATE))
			continue;

		base = its->base;

		/*
		 * Make sure that the ITS is disabled. If it fails to quiesce,
		 * don't restore it since writing to CBASER or BASER<n>
		 * registers is undefined according to the GIC v3 ITS
		 * Specification.
		 */
		ret = its_force_quiescent(base);
		if (ret) {
			pr_err("ITS@%pa: failed to quiesce on resume: %d\n",
			       &its->phys_base, ret);
			continue;
		}

		gits_write_cbaser(its->cbaser_save, base + GITS_CBASER);

		/*
		 * Writing CBASER resets CREADR to 0, so make CWRITER and
		 * cmd_write line up with it.
		 */
		its->cmd_write = its->cmd_base;
		gits_write_cwriter(0, base + GITS_CWRITER);

		/* Restore GITS_BASER from the value cache. */
		for (i = 0; i < GITS_BASER_NR_REGS; i++) {
			struct its_baser *baser = &its->tables[i];

			if (!(baser->val & GITS_BASER_VALID))
				continue;

			its_write_baser(its, baser, baser->val);
		}
		writel_relaxed(its->ctlr_save, base + GITS_CTLR);

		/*
		 * Reinit the collection if it's stored in the ITS. This is
		 * indicated by the col_id being less than the HCC field.
		 * CID < HCC as specified in the GIC v3 Documentation.
		 */
		if (its->collections[smp_processor_id()].col_id <
		    GITS_TYPER_HCC(gic_read_typer(base + GITS_TYPER)))
			its_cpu_init_collection(its);
	}
	raw_spin_unlock(&its_lock);
}

static struct syscore_ops its_syscore_ops = {
	.suspend = its_save_disable,
	.resume = its_restore_enable,
};

static int its_init_domain(struct fwnode_handle *handle, struct its_node *its)
{
	struct irq_domain *inner_domain;
	struct msi_domain_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	inner_domain = irq_domain_create_tree(handle, &its_domain_ops, its);
	if (!inner_domain) {
		kfree(info);
		return -ENOMEM;
	}

	inner_domain->parent = its_parent;
	irq_domain_update_bus_token(inner_domain, DOMAIN_BUS_NEXUS);
	inner_domain->flags |= its->msi_domain_flags;
	info->ops = &its_msi_domain_ops;
	info->data = its;
	inner_domain->host_data = info;

	return 0;
}

static int its_init_vpe_domain(void)
{
	struct its_node *its;
	u32 devid;
	int entries;

	if (gic_rdists->has_direct_lpi) {
		pr_info("ITS: Using DirectLPI for VPE invalidation\n");
		return 0;
	}

	/* Any ITS will do, even if not v4 */
	its = list_first_entry(&its_nodes, struct its_node, entry);

	entries = roundup_pow_of_two(nr_cpu_ids);
	vpe_proxy.vpes = kcalloc(entries, sizeof(*vpe_proxy.vpes),
				 GFP_KERNEL);
	if (!vpe_proxy.vpes) {
		pr_err("ITS: Can't allocate GICv4 proxy device array\n");
		return -ENOMEM;
	}

	/* Use the last possible DevID */
	devid = GENMASK(its->device_ids - 1, 0);
	vpe_proxy.dev = its_create_device(its, devid, entries, false);
	if (!vpe_proxy.dev) {
		kfree(vpe_proxy.vpes);
		pr_err("ITS: Can't allocate GICv4 proxy device\n");
		return -ENOMEM;
	}

	BUG_ON(entries > vpe_proxy.dev->nr_ites);

	raw_spin_lock_init(&vpe_proxy.lock);
	vpe_proxy.next_victim = 0;
	pr_info("ITS: Allocated DevID %x as GICv4 proxy device (%d slots)\n",
		devid, vpe_proxy.dev->nr_ites);

	return 0;
}

static int __init its_compute_its_list_map(struct resource *res,
					   void __iomem *its_base)
{
	int its_number;
	u32 ctlr;

	/*
	 * This is assumed to be done early enough that we're
	 * guaranteed to be single-threaded, hence no
	 * locking. Should this change, we should address
	 * this.
	 */
	its_number = find_first_zero_bit(&its_list_map, GICv4_ITS_LIST_MAX);
	if (its_number >= GICv4_ITS_LIST_MAX) {
		pr_err("ITS@%pa: No ITSList entry available!\n",
		       &res->start);
		return -EINVAL;
	}

	ctlr = readl_relaxed(its_base + GITS_CTLR);
	ctlr &= ~GITS_CTLR_ITS_NUMBER;
	ctlr |= its_number << GITS_CTLR_ITS_NUMBER_SHIFT;
	writel_relaxed(ctlr, its_base + GITS_CTLR);
	ctlr = readl_relaxed(its_base + GITS_CTLR);
	if ((ctlr & GITS_CTLR_ITS_NUMBER) != (its_number << GITS_CTLR_ITS_NUMBER_SHIFT)) {
		its_number = ctlr & GITS_CTLR_ITS_NUMBER;
		its_number >>= GITS_CTLR_ITS_NUMBER_SHIFT;
	}

	if (test_and_set_bit(its_number, &its_list_map)) {
		pr_err("ITS@%pa: Duplicate ITSList entry %d\n",
		       &res->start, its_number);
		return -EINVAL;
	}

	return its_number;
}

static int __init its_probe_one(struct resource *res,
				struct fwnode_handle *handle, int numa_node)
{
	struct its_node *its;
	void __iomem *its_base;
	u32 val, ctlr;
	u64 baser, tmp, typer;
	struct page *page;
	int err;

	its_base = ioremap(res->start, resource_size(res));
	if (!its_base) {
		pr_warn("ITS@%pa: Unable to map ITS registers\n", &res->start);
		return -ENOMEM;
	}

	val = readl_relaxed(its_base + GITS_PIDR2) & GIC_PIDR2_ARCH_MASK;
	if (val != 0x30 && val != 0x40) {
		pr_warn("ITS@%pa: No ITS detected, giving up\n", &res->start);
		err = -ENODEV;
		goto out_unmap;
	}

	err = its_force_quiescent(its_base);
	if (err) {
		pr_warn("ITS@%pa: Failed to quiesce, giving up\n", &res->start);
		goto out_unmap;
	}

	pr_info("ITS %pR\n", res);

	its = kzalloc(sizeof(*its), GFP_KERNEL);
	if (!its) {
		err = -ENOMEM;
		goto out_unmap;
	}

	raw_spin_lock_init(&its->lock);
	mutex_init(&its->dev_alloc_lock);
	INIT_LIST_HEAD(&its->entry);
	INIT_LIST_HEAD(&its->its_device_list);
	typer = gic_read_typer(its_base + GITS_TYPER);
	its->base = its_base;
	its->phys_base = res->start;
	its->ite_size = GITS_TYPER_ITT_ENTRY_SIZE(typer);
	its->device_ids = GITS_TYPER_DEVBITS(typer);
	its->is_v4 = !!(typer & GITS_TYPER_VLPIS);
	if (its->is_v4) {
		if (!(typer & GITS_TYPER_VMOVP)) {
			err = its_compute_its_list_map(res, its_base);
			if (err < 0)
				goto out_free_its;

			its->list_nr = err;

			pr_info("ITS@%pa: Using ITS number %d\n",
				&res->start, err);
		} else {
			pr_info("ITS@%pa: Single VMOVP capable\n", &res->start);
		}
	}

	its->numa_node = numa_node;

	page = alloc_pages_node(its->numa_node, GFP_KERNEL | __GFP_ZERO,
				get_order(ITS_CMD_QUEUE_SZ));
	if (!page) {
		err = -ENOMEM;
		goto out_free_its;
	}
	its->cmd_base = (void *)page_address(page);
	its->cmd_write = its->cmd_base;
	its->fwnode_handle = handle;
	its->get_msi_base = its_irq_get_msi_base;
	its->msi_domain_flags = IRQ_DOMAIN_FLAG_MSI_REMAP;

	its_enable_quirks(its);

	err = its_alloc_tables(its);
	if (err)
		goto out_free_cmd;

	err = its_alloc_collections(its);
	if (err)
		goto out_free_tables;

	baser = (virt_to_phys(its->cmd_base)	|
		 GITS_CBASER_RaWaWb		|
		 GITS_CBASER_InnerShareable	|
		 (ITS_CMD_QUEUE_SZ / SZ_4K - 1)	|
		 GITS_CBASER_VALID);

	gits_write_cbaser(baser, its->base + GITS_CBASER);
	tmp = gits_read_cbaser(its->base + GITS_CBASER);

	if ((tmp ^ baser) & GITS_CBASER_SHAREABILITY_MASK) {
		if (!(tmp & GITS_CBASER_SHAREABILITY_MASK)) {
			/*
			 * The HW reports non-shareable, we must
			 * remove the cacheability attributes as
			 * well.
			 */
			baser &= ~(GITS_CBASER_SHAREABILITY_MASK |
				   GITS_CBASER_CACHEABILITY_MASK);
			baser |= GITS_CBASER_nC;
			gits_write_cbaser(baser, its->base + GITS_CBASER);
		}
		pr_info("ITS: using cache flushing for cmd queue\n");
		its->flags |= ITS_FLAGS_CMDQ_NEEDS_FLUSHING;
	}

	gits_write_cwriter(0, its->base + GITS_CWRITER);
	ctlr = readl_relaxed(its->base + GITS_CTLR);
	ctlr |= GITS_CTLR_ENABLE;
	if (its->is_v4)
		ctlr |= GITS_CTLR_ImDe;
	writel_relaxed(ctlr, its->base + GITS_CTLR);

	if (GITS_TYPER_HCC(typer))
		its->flags |= ITS_FLAGS_SAVE_SUSPEND_STATE;

	err = its_init_domain(handle, its);
	if (err)
		goto out_free_tables;

	raw_spin_lock(&its_lock);
	list_add(&its->entry, &its_nodes);
	raw_spin_unlock(&its_lock);

	return 0;

out_free_tables:
	its_free_tables(its);
out_free_cmd:
	free_pages((unsigned long)its->cmd_base, get_order(ITS_CMD_QUEUE_SZ));
out_free_its:
	kfree(its);
out_unmap:
	iounmap(its_base);
	pr_err("ITS@%pa: failed probing (%d)\n", &res->start, err);
	return err;
}

static bool gic_rdists_supports_plpis(void)
{
	return !!(gic_read_typer(gic_data_rdist_rd_base() + GICR_TYPER) & GICR_TYPER_PLPIS);
}

static int redist_disable_lpis(void)
{
	void __iomem *rbase = gic_data_rdist_rd_base();
	u64 timeout = USEC_PER_SEC;
	u64 val;

	if (!gic_rdists_supports_plpis()) {
		pr_info("CPU%d: LPIs not supported\n", smp_processor_id());
		return -ENXIO;
	}

	val = readl_relaxed(rbase + GICR_CTLR);
	if (!(val & GICR_CTLR_ENABLE_LPIS))
		return 0;

	/*
	 * If coming via a CPU hotplug event, we don't need to disable
	 * LPIs before trying to re-enable them. They are already
	 * configured and all is well in the world.
	 *
	 * If running with preallocated tables, there is nothing to do.
	 */
	if (gic_data_rdist()->lpi_enabled ||
	    (gic_rdists->flags & RDIST_FLAGS_RD_TABLES_PREALLOCATED))
		return 0;

	/*
	 * From that point on, we only try to do some damage control.
	 */
	pr_warn("GIC-2500: CPU%d: Booted with LPIs enabled, memory probably corrupted\n",
		smp_processor_id());
	add_taint(TAINT_CRAP, LOCKDEP_STILL_OK);

	/* Disable LPIs */
	val &= ~GICR_CTLR_ENABLE_LPIS;
	writel_relaxed(val, rbase + GICR_CTLR);

	/* Make sure any change to GICR_CTLR is observable by the GIC */
	dsb(sy);

	/*
	 * Software must observe RWP==0 after clearing GICR_CTLR.EnableLPIs
	 * from 1 to 0 before programming GICR_PEND{PROP}BASER registers.
	 * Error out if we time out waiting for RWP to clear.
	 */
	while (readl_relaxed(rbase + GICR_CTLR) & GICR_CTLR_RWP) {
		if (!timeout) {
			pr_err("CPU%d: Timeout while disabling LPIs\n",
			       smp_processor_id());
			return -ETIMEDOUT;
		}
		udelay(1);
		timeout--;
	}

	/*
	 * After it has been written to 1, it is IMPLEMENTATION
	 * DEFINED whether GICR_CTLR.EnableLPI becomes RES1 or can be
	 * cleared to 0. Error out if clearing the bit failed.
	 */
	if (readl_relaxed(rbase + GICR_CTLR) & GICR_CTLR_ENABLE_LPIS) {
		pr_err("CPU%d: Failed to disable LPIs\n", smp_processor_id());
		return -EBUSY;
	}

	return 0;
}

int phytium_its_cpu_init(void)
{
	if (!list_empty(&its_nodes)) {
		int ret;

		ret = redist_disable_lpis();
		if (ret)
			return ret;

		its_cpu_init_lpis();
		its_cpu_init_collections();
	}

	return 0;
}

static const struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

static int __init its_of_probe(struct device_node *node)
{
	struct device_node *np;
	struct resource res;

	for (np = of_find_matching_node(node, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		if (!of_device_is_available(np))
			continue;
		if (!of_property_read_bool(np, "msi-controller")) {
			pr_warn("%pOF: no msi-controller property, ITS ignored\n",
				np);
			continue;
		}

		if (of_address_to_resource(np, 0, &res)) {
			pr_warn("%pOF: no regs?\n", np);
			continue;
		}

		its_probe_one(&res, &np->fwnode, of_node_to_nid(np));
	}
	return 0;
}

#ifdef CONFIG_ACPI

#define ACPI_GICV3_ITS_MEM_SIZE (SZ_128K)

#ifdef CONFIG_ACPI_NUMA
struct its_srat_map {
	/* numa node id */
	u32	numa_node;
	/* GIC ITS ID */
	u32	its_id;
};

static struct its_srat_map *its_srat_maps __initdata;
static int its_in_srat __initdata;

static int __init acpi_get_its_numa_node(u32 its_id)
{
	int i;

	for (i = 0; i < its_in_srat; i++) {
		if (its_id == its_srat_maps[i].its_id)
			return its_srat_maps[i].numa_node;
	}
	return NUMA_NO_NODE;
}

static int __init gic_acpi_match_srat_its(union acpi_subtable_headers *header,
					  const unsigned long end)
{
	return 0;
}

static int __init gic_acpi_parse_srat_its(union acpi_subtable_headers *header,
			 const unsigned long end)
{
	int node;
	struct acpi_srat_gic_its_affinity *its_affinity;

	its_affinity = (struct acpi_srat_gic_its_affinity *)header;
	if (!its_affinity)
		return -EINVAL;

	if (its_affinity->header.length < sizeof(*its_affinity)) {
		pr_err("SRAT: Invalid header length %d in ITS affinity\n",
			its_affinity->header.length);
		return -EINVAL;
	}

	node = acpi_map_pxm_to_node(its_affinity->proximity_domain);

	if (node == NUMA_NO_NODE || node >= MAX_NUMNODES) {
		pr_err("SRAT: Invalid NUMA node %d in ITS affinity\n", node);
		return 0;
	}

	its_srat_maps[its_in_srat].numa_node = node;
	its_srat_maps[its_in_srat].its_id = its_affinity->its_id;
	its_in_srat++;
	pr_info("SRAT: PXM %d -> ITS %d -> Node %d\n",
		its_affinity->proximity_domain, its_affinity->its_id, node);

	return 0;
}

static void __init acpi_table_parse_srat_its(void)
{
	int count;

	count = acpi_table_parse_entries(ACPI_SIG_SRAT,
			sizeof(struct acpi_table_srat),
			ACPI_SRAT_TYPE_GIC_ITS_AFFINITY,
			gic_acpi_match_srat_its, 0);
	if (count <= 0)
		return;

	its_srat_maps = kmalloc_array(count, sizeof(struct its_srat_map),
				      GFP_KERNEL);
	if (!its_srat_maps) {
		pr_warn("SRAT: Failed to allocate memory for its_srat_maps!\n");
		return;
	}

	acpi_table_parse_entries(ACPI_SIG_SRAT,
			sizeof(struct acpi_table_srat),
			ACPI_SRAT_TYPE_GIC_ITS_AFFINITY,
			gic_acpi_parse_srat_its, 0);
}

/* free the its_srat_maps after ITS probing */
static void __init acpi_its_srat_maps_free(void)
{
	kfree(its_srat_maps);
}
#else
static void __init acpi_table_parse_srat_its(void)	{ }
static int __init acpi_get_its_numa_node(u32 its_id) { return NUMA_NO_NODE; }
static void __init acpi_its_srat_maps_free(void) { }
#endif

static int __init gic_acpi_parse_madt_its(union acpi_subtable_headers *header,
					  const unsigned long end)
{
	struct acpi_madt_generic_translator *its_entry;
	struct fwnode_handle *dom_handle;
	struct resource res;
	int err;

	its_entry = (struct acpi_madt_generic_translator *)header;
	memset(&res, 0, sizeof(res));
	res.start = its_entry->base_address;
	res.end = its_entry->base_address + ACPI_GICV3_ITS_MEM_SIZE - 1;
	res.flags = IORESOURCE_MEM;

	dom_handle = irq_domain_alloc_fwnode(&res.start);
	if (!dom_handle) {
		pr_err("ITS@%pa: Unable to allocate GIC-phytium-2500 ITS domain token\n",
		       &res.start);
		return -ENOMEM;
	}

	err = iort_register_domain_token(its_entry->translation_id, res.start,
					 dom_handle);
	if (err) {
		pr_err("ITS@%pa: Unable to register GIC-phytium-2500 ITS domain token (ITS ID %d) to IORT\n",
		       &res.start, its_entry->translation_id);
		goto dom_err;
	}

	err = its_probe_one(&res, dom_handle,
			acpi_get_its_numa_node(its_entry->translation_id));
	if (!err)
		return 0;

	iort_deregister_domain_token(its_entry->translation_id);
dom_err:
	irq_domain_free_fwnode(dom_handle);
	return err;
}

static void __init its_acpi_probe(void)
{
	acpi_table_parse_srat_its();
	acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_TRANSLATOR,
			      gic_acpi_parse_madt_its, 0);
	acpi_its_srat_maps_free();
}
#else
static void __init its_acpi_probe(void) { }
#endif

int __init phytium_its_init(struct fwnode_handle *handle, struct rdists *rdists,
		    struct irq_domain *parent_domain)
{
	struct device_node *of_node;
	struct its_node *its;
	bool has_v4 = false;
	int err;

	its_parent = parent_domain;
	of_node = to_of_node(handle);
	if (of_node)
		its_of_probe(of_node);
	else
		its_acpi_probe();

	if (list_empty(&its_nodes)) {
		pr_warn("ITS: No ITS available, not enabling LPIs\n");
		return -ENXIO;
	}

	gic_rdists = rdists;

	err = allocate_lpi_tables();
	if (err)
		return err;

	list_for_each_entry(its, &its_nodes, entry)
		has_v4 |= its->is_v4;

	if (has_v4 & rdists->has_vlpis) {
		if (its_init_vpe_domain() ||
		    its_init_v4(parent_domain, &its_vpe_domain_ops)) {
			rdists->has_vlpis = false;
			pr_err("ITS: Disabling GICv4 support\n");
		}
	}

	register_syscore_ops(&its_syscore_ops);

	return 0;
}
