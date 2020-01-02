/******************************************************************************
* FileName: 
*   	ssd_adv.c
* Date:
*   	2008.9
*
* Description:
*     Driver for SSD
*
* Platform: 
*   	Linux kernel 2.6.9/2.6.18
*
* Designer:
*     SSD DEV Team
*
* History:
******************************************************************************/
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
#include <linux/config.h>
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include <linux/scatterlist.h>  
//#include <asm/msr.h>
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/list.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>

#undef DEV_NAME
#define DEV_NAME			"ssd"

#define MODULE_NAME			"ssd_adv"
#define DRIVER_VERSION		"1.0-0.11"
#define FPGA_VERSION_MAX	0x89
#define FPGA_VERSION_MIN	0x8

/* NEW VERSION CONTROLLER FOR 256 PAGES MLC */
#define FPGA_VER_NEW		0x25
#define FPGA_VER_NEW_L1		0x51
#define FPGA_VER_NEW_L2		0x71

#define SSD_MINORS			16
#define SSD_MAX_SG_SIZE		32

#define NFR_ADDR_SHIFT	10
#define NFR_DATA_SHIFT	0
#define NFR_RW_SHIFT	9
#define NFR_STA_SHIFT	8
#define NOR_FLASH_REG 	0x68

#define SSD_CMD_TIMEOUT 50000

#define MAX_CARD			4
#define FPGA_PER_CARD		4
#define MAX_FPGA_NUM		(MAX_CARD*FPGA_PER_CARD)
#define MAX_FLASH_PER_FPGA	32
#define MAX_CHIPS_PER_FLASH	2
#define MAX_CHIPS			(MAX_FLASH_PER_FPGA*MAX_CHIPS_PER_FLASH)

#define BLOCK_RESERVED_FOR_BBT	8
#define BBT_FLAG			"!TBB"
#define BBT_FLAG_SIZE		4
#define BBT_FLAG_PAGE		2
#define BBT_BITS_PERBLOCK	2

#define RAM_SIZE			(64*1024*1024)
#define RAM_SIZE_BLKSIZE_BASE (128*4096)

#define RAM_9STEP_YARD		(1024)
#define RAM_WORD_LEN		8
#define RAM_BBT_BASE_ADDR	0x3F00000
#define RAM_BBT_SIZE		0x2000
#define RAM_BBT_PER_WORD	(64/BBT_BITS_PERBLOCK)

#define RAM_OP_SIZE			(4*1024)

#define SSD_CMD_FIFO_DEPTH	32
#define SSD_MAX_RESP_Q_LEN  32

typedef struct ssd_op_info_user {
        uint64_t start;
        uint32_t length;
        uint32_t done;
        uint8_t __user * buf;
}ssd_op_info_user_t;

typedef struct ssd_flash_op_info_user {
	uint16_t flash;
	uint16_t chip; //no use!
	uint32_t page;
	uint32_t pagenum;
	uint32_t done;
	uint8_t __user * buf;
}ssd_flash_op_info_user_t;

typedef struct ssd_bbt_info_user {
	uint16_t flash;
	uint16_t length;
	uint16_t mode;
	uint16_t done;
	uint8_t __user * buf;
}ssd_bbt_info_user_t;

typedef struct ssd_bitflip_info_user {
	uint64_t bitflip[16];
	uint64_t read_amount;
}ssd_bitflip_info_user_t;

typedef struct ssd_mark_info_user {
	uint16_t flash;
	uint16_t ok;
	uint32_t block;
}ssd_mark_info_user_t;


typedef struct ssd_info_user {
	uint64_t size;
	uint64_t flash_status;
	uint64_t phys_flash_status;
	uint64_t phys_init_state;
	uint64_t flashsize;
	uint64_t blockcount;		//per flash
	uint32_t flashcount;		//per fpga
	uint32_t phys_flashcount;
	uint32_t blocksize;
	uint32_t pagesize;
	uint16_t oobsize;
	uint16_t pagecount;
	uint32_t version;
} ssd_info_user_t;

typedef struct ssd_id_info_user {
	uint32_t chip;
	uint32_t length;
	uint8_t* __user buf;
}__attribute__((packed))ssd_id_info_user_t;

typedef struct ssd_init_status_user {
        uint8_t status[32];
}__attribute__((packed))ssd_init_status_user_t; 

typedef struct ssd_log_info_user
{
	struct timeval time;
	uint32_t count;
	uint32_t idx;
	uint8_t * __user buf;	 
}__attribute__((packed))ssd_log_info_user_t; 

typedef struct cardinfo
{
	uint32_t count;
	uint8_t card[MAX_CARD];
}__attribute__((packed))cardinfo_t;

typedef struct ssd_pcbinfo
{
	uint8_t  pcb_ver;
	uint8_t  upper_pcb_ver;
	uint16_t index;
} ssd_pcbinfo_t;

/***********FUN ID DEFINE*********************/
#define  FUN_READ						0x01
#define  FUN_WRITE						0x02
#define  FUN_NAND_CHIP_READ_WITH_OOB	0x03
#define  FUN_NAND_CHIP_READ				0x04
#define  FUN_NAND_CHIP_WRITE			0x05
#define  FUN_NAND_CHIP_ERASE			0x06
#define  FUN_GET_CHIP_ID				0x07                 
#define  FUN_GET_LOG					0x08
#define  FUN_RAM_READ					0x10
#define  FUN_RAM_WRITE					0x11
#define  FUN_FLUSH_CACHE_OR_BBT			0x12

/***********Register define**********************/
#define REQ_FIFO_REG			0x0000
#define SSD_CLEAR_INTR_REG		0x0008
#define RESP_FIFO_REG			0x0010
#define RESP_FIFO_P_REG			0x0018
#define FLASH_ID_CAP_REG		0x0020
#define FLASH_CHIP_REG			0x0028
#define FPGA_VER_REG			0x0030
#define CPLD_VER_REG			0x0038
#define CARD_TYPE_REG			0x003C
#define PCB_VER_REG				0x0040
#define FPGA_STATUS_REG			0x0048
#define FPGA_RESET_REG			0x0078
#define FPGA_INT_INTERVAL_REG	0x0080

#define FPGA_INIT_STATE_REG		0x0048
#define FPGA_FLASH_STATUS_REG	0x0028
#define FPGA_STRIPED_REG		0x0088
#define FPGA_FLIP_REG			0x0090

#define FPGA_BURN_FLAG_REG		0x0098

#define FPGA_ID_REG				0x00A0

#define FPGA_SSD_STATUS_REG		0x00E8
#define FPGA_SSD_STATUS_REG1	0x00F0
#define FPGA_SSD_STATUS_REG2	0x00F8




/***********Ioctl define***********************/

#define CMD_NOR_ERASE		_IOW('H',0,struct erase_info)
#define CMD_NOR_WRITE		_IOW('H',1,struct rw_info)
#define CMD_NOR_READ		_IOR('H',2,struct rw_info)

#define CMD_NAND_READ           _IOWR('H',20,struct ssd_flash_op_info_user)
#define CMD_NAND_WRITE	        _IOWR('H',21,struct ssd_flash_op_info_user)
#define CMD_NAND_ERASE          _IOWR('H',22,struct ssd_flash_op_info_user)
#define CMD_GET_LOG             _IOR('H',23,struct ssd_log_info_user)
#define CMD_NAND_FPGA_STATUS    _IOR('H',24,uint64_t)
#define CMD_NAND_GET_PCB_VER    _IOR('H',25,uint32_t)
#define CMD_NAND_GET_CPLD_VER   _IOR('H',26,uint32_t)
#define CMD_FPGA_RESET		    _IOW('H',27,int)
#define CMD_RAM_READ            _IOWR('H',28,struct ssd_op_info_user)
#define CMD_RAM_WRITE				_IOWR('H',29,struct ssd_op_info_user)
#define CMD_RAM_9STEP				_IOW('H',30,uint32_t)
#define CMD_RAM_3STEP				_IOW('H',31,uint32_t)
#define CMD_SSD_BLOCK_IS_BAD		_IOW('H',32,loff_t)
#define CMD_SSD_BLOCK_IS_BAD_DIRECT	_IOW('H',33,struct ssd_flash_op_info_user)
#define CMD_SSD_MARK_BAD			_IOW('H',34, struct ssd_mark_info_user)
#define CMD_SSD_UPDATE_BBT			_IOW('H',35,uint32_t)
#define CMD_NAND_READ_WITH_OOB		_IOWR('H',36,struct ssd_flash_op_info_user)
#define CMD_NAND_GET_CHIPID			_IOR('H',37,struct ssd_id_info_user)
#define CMD_FPGA_GET_INFO			_IOR('H',38,struct ssd_info_user)
#define CMD_SSD_GET_BBT				_IOWR('H',38,struct ssd_bbt_info_user)
#define CMD_SSD_GET_BITFLIP			_IOWR('H',39,struct ssd_bitflip_info_user)
#define CMD_SSD_ERASE_BBT			_IOW('H',40, int)
#define CMD_SSD_BURN_FLAG			_IOW('H',41, int)
#define CMD_SSD_RELOAD_FPGA			_IOW('H',42, int)
#define CMD_GET_CARD_NUM			_IOW('H',43, struct cardinfo)
#define CMD_GET_SSD_STATUS			_IOWR('H',45, uint64_t)
#define CMD_GET_PCB_VER				_IOWR('H',47, struct ssd_pcbinfo)
#define CMD_SSDLOGD_PID				_IOW('H',50, pid_t)



#define SHOW_BITFLIP
enum ssd_drv_mode
{
	SSD_DRV_MODE_STANDARD = 0,
	SSD_DRV_MODE_RB_COMPATIBILITY = 1,
	SSD_DRV_MODE_DEBUG = 2,
};

enum ssd_cardtype
{
	SSD_CARD_TYPE1 = 0,
	SSD_CARD_TYPE2 = 1,
};

enum 
{
	/* command message queue limits */
	SSD_MAX_CTRL_MSG		= 32,	       /* max command msgs per controller */
	SSD_CTRL_MSG_FULL = (1<<5),


	/* S/G limits, sdev-wide and per-request */
	SSD_MAX_RW_MSG = 32,            /* max  r/w request per controller*/
//	SSD_MAX_RW_SG = 128,            /* max s/g entries per request*/
	SSD_MAX_RW_SG_FULL = (1<<7),
	SSD_MSG_MASK = 0x000000000000001FULL,
	SSD_DEFAULT_MSG = 0xFFFFFFFFFFFFFFFFULL,
	SSD_DEFAULT_RESP_IDX = 0x3F,

	SSD_MAX_TAGS = 32,

	SSD_MAX_LOG = 512,
	SSD_LOG_SIZE = 8,

	//SSD_MAX_FPGA_RAM_SIZE = 0x4000000,
	SSD_MAX_CHIP_ID_SIZE = 8,

	SSD_FPGA_RESET_NOINIT = 0x52535431,
	SSD_FPGA_RESET_INIT = 0x52535432,
	SSD_FPGA_RESET_TIMEOUT = 200,

	SSD_FLASH_INIT_STATE_MASK	= 0x01,
	SSD_INIT_STATE_MASK	= 0xFFFFFFFF,
	SSD_FLASH_STATUS_BITS = 2,
	SSD_FLASH_STATUS_MASK = 0x03,
	SSD_FLASH_EXIST_MASK = 0x01,
	SSD_FLASH_CHIPS_MASK = 0x02,
	SSD_FLASH_HALF_STATUS = 0x5555555555555555ULL,
};

#pragma pack(8)

typedef struct ssd_sg_entry
{
    uint32_t block_start_low;
    uint16_t block_start_high;
    uint16_t length;
    uint32_t buf_addr_low;
    uint32_t buf_addr_high;
}ssd_sg_entry_t;

typedef struct ssd_rw_msg
{
    uint8_t msg_seq;
    uint8_t msg_fg;
    uint8_t sg_num;
    uint8_t fun;
    struct ssd_sg_entry ssd_sg_entry[SSD_MAX_SG_SIZE];
}ssd_rw_msg_t;

#pragma pack()

typedef struct ssd_resp_msg
{
    uint8_t msg_seq;
    uint8_t status;
    uint8_t notify:1;
	uint8_t reserved:3;
	uint8_t bit_flip:4;
    uint8_t fun;
    uint32_t reserved1;
}__attribute__((packed))ssd_resp_msg_t;

/******************LOG FUN define******************************/
typedef struct ssd_log_msg
{
     uint8_t msg_seq;
     uint16_t log_count;
     uint8_t fun;
     uint32_t buf_addr_low;
     uint32_t buf_add_high;
}__attribute__((packed))ssd_log_msg_t;

typedef struct ssd_log
{
    uint16_t log_seq;
    uint16_t event_type;
    uint8_t  chip;
    uint16_t block;
    uint8_t page;
}__attribute__((packed))ssd_log_t;

typedef struct ssd_log_entry
{
	uint64_t log_data;
}__attribute__((packed))ssd_log_entry_t;

typedef struct ssd_log_resp_msg
{
    uint8_t msg_tag;
    uint16_t stat :4;
    uint16_t record_count : 12;
    uint8_t fun;
	uint32_t reserved;
}__attribute__((packed))ssd_log_resp_msg_t;

typedef union ssd_response_msq
{
	ssd_resp_msg_t resp_msg;
	ssd_log_resp_msg_t logresp_msg;
	uint64_t uint_msg;
} ssd_response_msq_t;

enum resp_stat
{
    OP_SUCCESS = 0,
    OP_FAILED = 0x01,
    DEV_BUSY = 0x02,
    DMA_ADDR_ERR = 0x03,
};

enum fpga_init_stat
{
    FPGA_INIT_SUCCESS = 0,
    CHIP_ID_ERR = 1, 
    WRITE_BBT_ERR = 2,
    SSD_ECC_ERR = 3,
    CHIP_NOT_EXIT = 0xFE,
    CHIP_INITING = 0xFF,
};

typedef struct ssd_flush_cache_or_bbt_msg
{
    uint8_t msg_tag;
    uint8_t msg_flg;		//flash cache 0 or bbt 1
    uint8_t flash;
    uint8_t fun;
}__attribute__((packed))ssd_flush_cahce_msg_t;

typedef struct ssd_chip_op_msg
{
    uint8_t msg_seq;
    uint8_t msg_flg;
    uint8_t reserved1;
    uint8_t fun;
    uint16_t page_count;
    uint8_t reserved2;
    uint8_t chip_no;
    uint32_t page_no;
    uint32_t buf_addr_low;
    uint32_t buf_addr_high;
}__attribute__((packed))ssd_chip_op_msg_t;

typedef struct ssd_fpga_ram_op_msg
{
    uint8_t msg_seq;
    uint8_t msg_flg;
    uint8_t reserved;
    uint8_t fun; 
    uint32_t fpga_ram_addr;
    uint32_t length;
    uint32_t buf_addr_low;
    uint32_t buf_addr_high;
}__attribute__((packed))ssd_fpga_ram_op_msg_t;

struct erase_info
{
	uint32_t sect;
	uint32_t count;
};

struct rw_info
{
	uint8_t * user_buf;
	uint32_t  addr;
	uint32_t  len;
};

typedef struct ssd_fpga_info
{
	uint8_t   manufacturer;
	uint8_t   dev_id;
	uint16_t  block_count;	//per flash
	uint16_t  page_count;	//per block
	uint16_t  page_size;
	uint16_t  oobsize;
	uint8_t   flash_count;	//per fpga;
	uint8_t   phys_flash_count;
	uint64_t  flash_status; // flash status, exist or not, chip count;
	uint64_t  phys_flash_status;
	uint64_t  phys_init_state;
	uint32_t  block_size;
	uint64_t  flash_size;
	uint64_t  phy_size;
	uint64_t  pagemask;
	uint64_t  blockmask;
	uint64_t  flashmask;
	uint8_t   page_shift;
	uint8_t   block_shift;
	uint8_t   flash_shift;
	uint32_t  ramsize;
}__attribute__((packed)) ssd_fpga_info_t;

typedef struct ssd_tag_entry
{
	struct ssd_device * sdev;
	uint8_t tag;
	uint8_t status;
	uint16_t done;
	uint32_t pad;
	struct list_head eh_entry;
}__attribute__((packed)) ssd_tag_entry_t;

typedef struct ssd_device { 
	char name[10];
	struct pci_dev *pdev;
	struct list_head list;

	void* parent;
	//struct cdev cdev;
	int major; 
	void __iomem *ctrlp;

	int controller_idx;
	uint32_t fpga_ver;
//	volatile unsigned short device_busy;
	//	spinlock_t sdev_lock;

	struct workqueue_struct *workqueue;
	//struct list_head log_list_ssd;
	//struct file *filp;
	//char filp_name[32];
	struct work_struct read_wk;

	//uint64_t ioctl_flg;

	//high 32bit status ;low 32bit in/out  for I/O
	//uint64_t rq_flg;
	//      spinlock_t map_lock;
	//uint64_t	 ssd_msg_map;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	kmem_cache_t *cmd_slab;
#else
	struct kmem_cache *cmd_slab;
#endif
	spinlock_t		free_list_lock;
	struct list_head	free_list; /* backup store of cmd structs */

	//      spinlock_t seq_lock;
	//	int tag2seq[SSD_MAX_SEQS];
	uint32_t      *dmatable_cpu[SSD_CMD_FIFO_DEPTH];
	dma_addr_t  dmatable_dma[SSD_CMD_FIFO_DEPTH];
	uint64_t     resp_idx;

	void *resp_point_addr;
	//   uint64_t *resp_stat_addr_base[SSD_MAX_RESP_Q_LEN];
	uint64_t *resp_stat_addr_base;
	dma_addr_t resp_point_dma_addr;
	//   dma_addr_t resp_stat_dma_addr[SSD_MAX_RESP_Q_LEN];
	dma_addr_t resp_stat_dma_addr;

	uint64_t size;

	struct ssd_fpga_info fpga_info;

	struct semaphore bbt_sema;
	uint64_t bbt_updated;
	//bad block table
	/*
	uint8_t *bbt;	
	struct semaphore bbt_sema;

	uint8_t *bbt_pos;
	uint8_t *bbt_updated;
	*/

	struct scatterlist   *sg_table[SSD_CMD_FIFO_DEPTH];
	struct request_queue *queue;    /* The device request queue */
	spinlock_t 	queue_lock;
	struct gendisk *gd;             /* The gendisk structure */
	struct timer_list flush_timer;
	//int busy;
	struct work_struct flush_wk;
//	struct completion thread_dead;
	//int exiting;

#ifdef SHOW_BITFLIP
	uint64_t bitflip[16];
	uint64_t read_amount;
#endif

	struct module	 *owner;
}ssd_device_t;

typedef struct ssd_card {
	char name[12];

	unsigned char bus;
	
	uint8_t  status; //non-zero : the SSD got some problem;
	
	uint8_t  dev_count;
	uint8_t  expect_dev_count;
	uint64_t controller_size;
	uint64_t size;
	uint32_t version;
	uint32_t cpld_ver;

	uint16_t type; //ssd type
	uint8_t  stripe_size;
	uint8_t  rb_percent;

	uint8_t  pcb_ver;
	uint8_t  upper_pcb_ver;
	uint16_t primary;
	void *   primary_dev;

	struct list_head ssd_list;
} ssd_card_t;

enum ssd_cmnd_from
{
	SSD_CMND_FROM_SLAB = 0,
	SSD_CMND_FROM_FREE = 1,
};

enum ssd_cmnd_type
{
	SSD_CMD_BLOCK = 0,
	SSD_CMD_OTHER = 1,
};


#define MAX_COMMAND_SIZE	32

struct ssd_cmnd
{
	struct ssd_device *device;

	struct list_head list;

	enum ssd_cmnd_type cmd_type;

	enum ssd_cmnd_from from;

	unsigned char cmnd[MAX_COMMAND_SIZE];
	unsigned int status;
	unsigned int log_count;
	int errors;

	struct timer_list eh_timeout;
	struct completion *waiting;
	
	struct request *request;
};

struct ssd_cfg
{
	uint32_t stripe_size;
	uint32_t rb_percent;
};

enum {
	SSD_STRIPE_4K  = 0,
	SSD_STRIPE_64K = 1,
};

enum {
	SSD_TYPE_SLC = 2048,
	SSD_TYPE_MLC = 4096,
};

#define SSD_SIGLOG        60

struct semaphore log_mutex;
pid_t log_pid = 0;
static int not_deal_log = 0;


#define SSD_RESERVED_PAGES_PER_BLOCKS				2
#define SSD_SLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH	12
#define SSD_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH	26

#define SSD_128P_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH 6
#define SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH 14
#define SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH_L1 30
/*
 * As suggention from Honglingyun, change this value from 62 to 54
 *
 * 2、请确认代码这一行的值是54，如果不是将其修改为54
 * #define SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH_L2 54
 *
 */
#define SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH_L2 54

#define SSD_DEFAULT_STRIPE_SIZE 					SSD_STRIPE_4K
#define SSD_DEFAULT_STATUS_MASK						0xFF

#define SSD_SLC_DEFAULT_RESERVED_BLOCKS				3
#define SSD_MLC_DEFAULT_RESERVED_BLOCKS				6
#define SSD_MAX_RESERVED_BLOCKS						90

#define SSD_OLD_VER_RESERVED_BLOCKS					5
#define SSD_OLD_VER_RESERVED_BLOCKS_NEW_ARITH		25


static struct ssd_device *ssd_table[MAX_FPGA_NUM];
static struct ssd_card *ssd_card_table[MAX_CARD];
static uint8_t ssd_cardtype_map[4] = {4, 1, 0, 0,}; // 4fpga card, 1fpga card
static int ssd_card_count = 0;

static int ssd_major = 0;

static uint32_t ssd_controller_count = 0;
static uint32_t ssd_init_ok_sum = 0;

static int reload_fpga = -1;

static DEFINE_PER_CPU(struct list_head, ssd_done_q);
static DEFINE_PER_CPU(struct tasklet_struct, ssd_tasklet);

#define SSD_DEFAULT_PARAM	0xFFFF

static int mode        = SSD_DRV_MODE_RB_COMPATIBILITY;
static int rb_percent  = SSD_DEFAULT_PARAM;
static int stripe_size = SSD_DEFAULT_PARAM;
static int status_mask = SSD_DEFAULT_STATUS_MASK;

module_param(mode,int,0);
module_param(rb_percent,int,0);
module_param(stripe_size, int, 0);
module_param(status_mask, int, 0);


MODULE_PARM_DESC(mode,"Driver load mode.");
MODULE_PARM_DESC(rb_percent, "Reserved blocks in percent for each logic flash. Enlarge this value will destroy the data in SSD!");
MODULE_PARM_DESC(stripe_size,"0 - 4k, 1 - 64k.");
MODULE_PARM_DESC(status_mask,"0 - no I/O error from SSD Controller.");


#undef SSD_IO_64

#ifdef SSD_IO_64
#define ioread64	readq
#define iowrite64	writeq
#warning using 64 bit IO
#endif

static inline void ssd_reg_write(void *addr, uint64_t val)
{
#ifdef SSD_IO_64
	iowrite64(val, addr);
#else
	iowrite32((uint32_t)val, addr);
	iowrite32((uint32_t)(val >> 32), addr + 4);
#endif
	wmb();
}

static inline void ssd_reg32_write(void *addr, uint32_t val)
{
	iowrite32((uint32_t)val, addr);
	wmb();
}

static inline uint64_t ssd_reg_read(void *addr)
{
	uint64_t val;

#ifdef SSD_IO_64
	val = ioread64(addr);
#else
	uint32_t val_lo, val_hi;

	val_lo = ioread32(addr);
	val_hi = ioread32(addr + 4);
	val = val_lo | ((uint64_t)val_hi << 32);
#endif
	rmb();
	return val;
}

static inline uint32_t ssd_reg32_read(void *addr)
{
	uint32_t val = ioread32(addr);
	rmb();
	return val;
}

static void ssd_init_fpga_info(struct ssd_device *sdev)
{
	int64_t value1;//,value2;
	int i;
	int status;

	value1 = ssd_reg_read(sdev->ctrlp + FLASH_ID_CAP_REG);
	//printk("value1 = %Lx\n",value1);

	sdev->fpga_info.page_size = (uint16_t)value1;
	sdev->fpga_info.page_count = (uint16_t)(value1>>16) ;
	sdev->fpga_info.block_count = (uint16_t)((value1 >> 32) + 1);
	sdev->fpga_info.dev_id = (uint8_t)(value1 >> 48);
	sdev->fpga_info.manufacturer = (uint8_t)(value1 >> 56);

	
	sdev->fpga_info.oobsize = (sdev->fpga_info.page_size) >> 4;

	sdev->fpga_info.block_size = (uint32_t )sdev->fpga_info.page_size * (uint32_t)sdev->fpga_info.page_count;

	sdev->fpga_info.ramsize = RAM_SIZE;
	if (sdev->fpga_info.block_size > RAM_SIZE_BLKSIZE_BASE) {
		sdev->fpga_info.ramsize = RAM_SIZE * (sdev->fpga_info.block_size/RAM_SIZE_BLKSIZE_BASE);
	}

	/*
	sdev->fpga_info.page_size = 4096;
	sdev->fpga_info.block_size = 512*1024;
	sdev->fpga_info.block_count = (2048*2);
	sdev->fpga_info.page_count = (512*1024)/4096;
	*/
	//value2 = ssd_reg_read(sdev->ctrlp + FLASH_CHIP_REG);
	//printk("value2 = %Lx\n",value2);

	sdev->fpga_info.phys_init_state   = ssd_reg_read(sdev->ctrlp + FPGA_INIT_STATE_REG);
	sdev->fpga_info.phys_flash_status = ssd_reg_read(sdev->ctrlp + FPGA_FLASH_STATUS_REG);
	for(i=0; i<MAX_FLASH_PER_FPGA; i++) {
		status = (sdev->fpga_info.phys_flash_status >> (i*SSD_FLASH_STATUS_BITS)) & SSD_FLASH_EXIST_MASK;
		if(status)
			sdev->fpga_info.phys_flash_count++;
	}
	if(sdev->fpga_info.phys_flash_count == MAX_FLASH_PER_FPGA/2) {
		sdev->fpga_info.flash_count = MAX_FLASH_PER_FPGA;
		sdev->fpga_info.flash_status = SSD_FLASH_HALF_STATUS;
	} else if(sdev->fpga_info.phys_flash_count == MAX_FLASH_PER_FPGA) {
		sdev->fpga_info.flash_count = MAX_FLASH_PER_FPGA;
		sdev->fpga_info.flash_status = sdev->fpga_info.phys_flash_status;
	} else {
		printk(KERN_WARNING "%s: some flash is missing\n", sdev->name);
		sdev->fpga_info.flash_count = sdev->fpga_info.phys_flash_count;
		sdev->fpga_info.flash_status = sdev->fpga_info.phys_flash_status;
	}
	//sdev->fpga_info.flash_count = (uint32_t)((value2)) + 1;

	sdev->fpga_info.flash_size = (uint64_t)(sdev->fpga_info.block_count) * (uint64_t)sdev->fpga_info.block_size;

	sdev->fpga_info.phy_size= (uint64_t)(sdev->fpga_info.flash_count) * sdev->fpga_info.flash_size;

	sdev->fpga_info.page_shift = ffs(sdev->fpga_info.page_size) - 1;
	sdev->fpga_info.block_shift = ffs(sdev->fpga_info.block_size) - 1;
	for(i=0;i<sizeof(int64_t); i+=sizeof(int)) {
		sdev->fpga_info.flash_shift = ffs((int)(sdev->fpga_info.flash_size >> i));
		if((sdev->fpga_info.flash_shift)) {
			sdev->fpga_info.flash_shift += i - 1;
			break;
		}
	}

	sdev->fpga_info.pagemask = sdev->fpga_info.page_size - 1;
	sdev->fpga_info.blockmask = sdev->fpga_info.block_size - 1;
	sdev->fpga_info.flashmask = sdev->fpga_info.flash_size - 1;

}


static struct ssd_cmnd *ssd_get_command(struct ssd_device *dev, int gfp_mask);
static void ssd_put_command(struct ssd_cmnd *cmd);


static int ssd_do_direct_req(struct ssd_device *dev, int rw, void* msg, int* done)
{
	DECLARE_COMPLETION(wait);
	struct request *rq;
	struct ssd_cmnd *cmd;
	struct request_queue *q = dev->queue;
	int ret = 0;

	rq = blk_get_request(q, rw, __GFP_RECLAIM);
	if(!rq)
		return -ENOMEM;

	rq->rq_disk = dev->gd;
	rq->special = msg;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	rq->flags |= REQ_NOMERGE;
#else
	rq->cmd_flags |= REQ_NOMERGE;
#endif

	cmd = ssd_get_command(dev, GFP_ATOMIC);
	if(!cmd) {
		blk_put_request(rq);
		return -ENOMEM;
	}

	cmd->cmd_type = SSD_CMD_OTHER;
	cmd->waiting = &wait;
	cmd->request = rq;
	rq->special = cmd;
	memcpy(cmd->cmnd, msg, MAX_COMMAND_SIZE);

	elv_add_request(q, rq, ELEVATOR_INSERT_BACK);
	__blk_run_queue(q);

	wait_for_completion(cmd->waiting);
	cmd->waiting = NULL;

	if(unlikely(cmd->errors == -ETIMEDOUT))
		ret = cmd->errors;
	else if(cmd->errors)
		ret = -EIO;

	if(done != NULL)
		*done = cmd->log_count;

	ssd_put_command(cmd);
	blk_put_request(rq);
	return ret;
}

static int ssd_check_flash(struct ssd_device *sdev, int flash, int chip)
{
	int status;

	if(flash > sdev->fpga_info.flash_count)
		return -EINVAL;
/*
	if(chip > MAX_CHIPS_PER_FLASH || flash > sdev->fpga_info.flash_count)
		return -EINVAL;
*/

	status = (sdev->fpga_info.flash_status >> (flash*SSD_FLASH_STATUS_BITS)) & SSD_FLASH_STATUS_MASK;
	if(!(status & SSD_FLASH_EXIST_MASK)) 
		return -EINVAL;
/*
	if(!(status & SSD_FLASH_CHIPS_MASK) && chip == 1)
		return -EINVAL;
*/
	return 0;
}

static int ssd_check_param(struct ssd_device *sdev, int flash, int chip, int page, int pagenum)
{
	if(!pagenum)
		return -EINVAL;

	if((page / sdev->fpga_info.page_count) != ((page+pagenum-1) / sdev->fpga_info.page_count))
		return -EINVAL;

	return ssd_check_flash(sdev, flash, chip);
}


static int ssd_chip_read(struct ssd_device *sdev, void *buf,
								int flash, int chip, int page, int pagenum)
{
	int ret = 0, length;
	dma_addr_t kbuf_dma_r;
	struct ssd_chip_op_msg msg;

	if(!buf) {
		printk(KERN_WARNING "%s: ssd_chip_read:no mem!\n", sdev->name);
		return -EINVAL;
	}

	ret= ssd_check_param(sdev, flash, chip, page, pagenum);
	if(ret)
		return ret;

	length = pagenum * sdev->fpga_info.page_size;

	kbuf_dma_r = pci_map_single(sdev->pdev,buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
	if (dma_mapping_error(kbuf_dma_r)) {
#else
	if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif
		printk(KERN_WARNING "%s: unable to map  read DMA buffer.\n", sdev->name);
		ret = -ENOMEM;
		goto out;
	}


	msg.fun = FUN_NAND_CHIP_READ;
	msg.chip_no = (uint8_t)((flash << 1) | chip);
	msg.page_no = page;
	msg.page_count = pagenum;
	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);
	/*
	printk(KERN_WARNING "seq = %x, fun = %x, chip_no=%x, page_no = %x, page_count = %x\n", msg->msg_seq, msg->fun, msg->chip_no, msg->page_no, msg->page_count);
	tmp = (uint32_t *)msg;
	for(i=0;i<5;i++) {
	printk("cmd[%d] = 0x%x\n", i, tmp[i]);
	}
	*/

	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_r,length,PCI_DMA_FROMDEVICE);

	if(ret && ret != -ETIMEDOUT) ret = -EBADMSG;

out:
	return ret;
}

static int ssd_chip_read_with_oob(struct ssd_device *sdev, void *buf, 
									int flash, int chip, int page, int pagenum)
{
	int ret = 0, length;
	dma_addr_t kbuf_dma_r;
	struct ssd_chip_op_msg msg;

	if(!buf) {
		printk(KERN_WARNING"%s: ssd_chip_read:no mem!\n", sdev->name);
		return -EINVAL;
	}

	ret= ssd_check_param(sdev, flash, chip, page, pagenum);
	if(ret)
		return ret;

	length = pagenum * (sdev->fpga_info.page_size + sdev->fpga_info.oobsize);
	kbuf_dma_r = pci_map_single(sdev->pdev,buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_r)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif

		printk(KERN_WARNING "%s: Unable to map  read DMA buffer.\n", sdev->name);
		ret = -ENOMEM;
		goto out;
	}

	msg.fun = FUN_NAND_CHIP_READ_WITH_OOB;
	msg.chip_no = (uint8_t)((flash << 1) | chip);
	msg.page_no = page;
	msg.page_count = pagenum;
	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);

	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_r,length,PCI_DMA_FROMDEVICE);

	if(ret && ret != -ETIMEDOUT) ret = -EBADMSG;

out:
	return ret;
}

#if 0
static int ssd_chip_write(struct ssd_device *sdev,void  *buf, size_t length, loff_t ofs)
{
	int ret = 0, chip_no, page_num = 0, realpage,page,rem;
	dma_addr_t kbuf_dma_w;
	size_t tmp_length;
	struct ssd_chip_op_msg msg;

	if(!buf) {
		printk(KERN_WARNING"SSD:ssd_chip_read:no mem!\n");
		ret = -EINVAL;
		goto out_err;
	}

	if(length <= 0) {
		printk(KERN_WARNING"SSD:ssd_chip_read:read length  is zero\n");
		ret = -EINVAL;
		goto out_err;      
	}

#if 0
	if(length > MAX_KMALLOC_SIZE)
	{
	printk(KERN_WARNING"ssd_chip_read:too big length ,set to MAX_KMALLOC_SIZE",length);
	length = MAX_KMALLOC_SIZE;
	}
#endif

	kbuf_dma_w = pci_map_single(sdev->pdev,buf, length, PCI_DMA_TODEVICE);
	if (dma_mapping_error(kbuf_dma_w)) {
		printk(KERN_WARNING "SSD:Unable to map  write DMA buffer.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	//chip_no = (ofs & sdev->fpga_info.flashmask) >> sdev->fpga_info.chip_shift;
	//chip_no |= (int)((ofs >> sdev->fpga_info.flash_shift) << 2);
	chip_no = (int)(ofs >> sdev->fpga_info.chip_shift);


	realpage = (int)(ofs >> sdev->fpga_info.page_shift);
	page = realpage & (sdev->fpga_info.pagemask);

	tmp_length = length;
	//rem = do_div(tmp_length,sdev->fpga_info.page_size);
	//page_num = tmp_length;
	rem = tmp_length % sdev->fpga_info.page_size;
	page_num = page_num / sdev->fpga_info.page_size;
	if(rem)
		page_num++;

//	memset(&msg,0,sizeof(struct ssd_chip_op_msg));
	msg.fun = FUN_NAND_CHIP_WRITE;
	msg.chip_no = chip_no;
	msg.page_no = page;
	msg.page_count = page_num;
	msg.buf_addr_low = (uint32_t)kbuf_dma_w;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_w >> 32);

	ret = ssd_do_direct_req(sdev, WRITE, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_w,length,PCI_DMA_TODEVICE);

out_err:
	return ret;
}
#endif

static int ssd_chip_erase(struct ssd_device *sdev, int flash, int chip, int page)
{
	int ret = 0;
	struct ssd_chip_op_msg msg;

	ret= ssd_check_param(sdev, flash, chip, page, sdev->fpga_info.page_count);
	if(ret)
		return ret;

	msg.fun = FUN_NAND_CHIP_ERASE;
	msg.chip_no = (uint8_t)((flash << 1) | chip);;
	msg.page_no = page;

	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, NULL);

	return ret;
}


static int ssd_get_chip_id(struct ssd_device *sdev, int chip, void* id)
{
	struct ssd_chip_op_msg msg;
	int ret = 0;
	dma_addr_t kbuf_dma_r;

	if(unlikely(!id))
		return -EINVAL;

	if(unlikely(chip > MAX_CHIPS || chip < 0))
		return -EINVAL; 

	kbuf_dma_r = pci_map_single(sdev->pdev,id, SSD_MAX_CHIP_ID_SIZE, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_r)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif

		printk(KERN_WARNING "%s: Unable to map  read DMA buffer.\n", sdev->name);
		ret = -ENOMEM;
		goto out_map;
	}

	msg.fun = FUN_GET_CHIP_ID;
	msg.chip_no = chip;

	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);

	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_r,SSD_MAX_CHIP_ID_SIZE,PCI_DMA_FROMDEVICE);

out_map:
	return ret;
}

static int ssd_reset_controller(struct ssd_device *dev, int mode)
{
	uint64_t init_state;
	int i;
	int ret = -EIO;

	if((mode != 0) && (mode != 1))
		return -EINVAL;

	if(mode == 0) //no reinit
		ssd_reg32_write(dev->ctrlp + FPGA_RESET_REG, SSD_FPGA_RESET_NOINIT);
	else		//reset & init
		ssd_reg32_write(dev->ctrlp + FPGA_RESET_REG, SSD_FPGA_RESET_INIT);

	for(i=0; i<SSD_FPGA_RESET_TIMEOUT; i++) {
		init_state = ssd_reg_read(dev->ctrlp + FPGA_INIT_STATE_REG);
		if((init_state & SSD_INIT_STATE_MASK) == SSD_INIT_STATE_MASK) {
			ret = 0;
			break;
			
		} else {
			//printk(KERN_WARNING "waiting for init.%d\n", i);
			msleep(1000);
		}
	}

	return ret;
}

#if 0
static int block_isbad(struct ssd_device *sdev, loff_t ofs)
{
	int res;
	uint64_t block = ofs;

	if(ofs > sdev->fpga_info.phy_size)
		return -EINVAL;
	//down(&sdev->bbt_sema);
	if(NULL == sdev->bbt) {
		//ret = ssd_read_bbt(sdev);
		if(0) {		
			//up(&sdev->bbt_sema);
			return 0;
		}
	}

	do_div(block, sdev->fpga_info.block_size);
	block *= 2;

	res = (sdev->bbt[block >> 3] >> (block & 0x06)) & 0x03;
//	printk("ofs=0x%llx, blocksize=0x%llx block = %d, res=%d\n", ofs, sdev->fpga_info.block_size, block, res);
	//up(&sdev->bbt_sema);
	return res;
}
#endif

static int block_isbad_direct(struct ssd_device *sdev, int flash, int chip, int page)
{
	int res = 0x03; // good block
	int ret;
	int i;
	int cur_page;
	uint8_t * kbuf;
//	uint64_t rem;
	int len;


	ret= ssd_check_param(sdev, flash, chip, page, sdev->fpga_info.page_count);
	if(ret)
		return ret;

	len = sdev->fpga_info.page_size + sdev->fpga_info.oobsize;
	kbuf = kmalloc(len, GFP_KERNEL);
	if(NULL == kbuf) {
		printk(KERN_WARNING "can not alloc kbuf\n");
		return -ENOMEM;
	}

	for(i=0; i<2; i++) { //MLC last two page, SLC first two page
		if(sdev->fpga_info.page_count == 256) {
			cur_page = page + i;
		}
		else if(sdev->fpga_info.page_size == 2048) {
			cur_page = page + i;
		} else {
			cur_page = page + sdev->fpga_info.page_count - (i + 1);
		}
		ret = ssd_chip_read_with_oob(sdev, kbuf, flash, chip, cur_page, 1); // read one page
		if(unlikely(ret < 0 && ret != -EBADMSG)) {
			printk(KERN_WARNING "ssd_chip_read_with_oob err ret =%d\n", ret);
			res = ret;
			goto out;
		}

		//printk(KERN_WARNING "oob %x, %x\n", kbuf[sdev->fpga_info.page_size], kbuf[sdev->fpga_info.page_size + 1]);
		if(kbuf[sdev->fpga_info.page_size] != 0xff) {
/*			printk(KERN_WARNING "bad!!!! ofs=0x%llx  cur_ofs=0x%llx 0x%x, 0x%x\n", ofs, cur_ofs,kbuf[sdev->fpga_info.page_size], kbuf[sdev->fpga_info.page_size + 1]);
			for(i=4000;i<readlen;i++) 
				printk("%x ", kbuf[i] );
			printk("\n");
*/

			res = 0x00;
			goto out;
		}
	}

//	printk("ofs=0x%llx, blocksize=0x%llx block = %d, res=%d\n", ofs, sdev->fpga_info.block_size, block, res);
	//up(&sdev->bbt_sema);

out:
	kfree(kbuf);
	return res;
}

#if 0
static int update_bbt(struct ssd_device *sdev)
{
	int ret = 0;
	int flash;
	uint64_t ofs, flag_ofs;
	uint8_t* kbuf = NULL;
	uint8_t* cur_bbt = NULL;
	uint32_t flash_bbt_len;
	uint32_t block, i;

	flash_bbt_len = sdev->fpga_info.block_count * sdev->fpga_info.chip_count * BBT_BITS_PERBLOCK;
	if(unlikely(flash_bbt_len > sdev->fpga_info.page_size))
		return -EPERM;

	down(&sdev->bbt_sema);
	for(flash=0; flash<sdev->fpga_info.flash_count; flash++) {
		if(1 == sdev->bbt_updated[flash]) {
			if(NULL == kbuf) {
				kbuf = kmalloc(sdev->fpga_info.page_size, GFP_KERNEL);
				if(NULL == kbuf) {
					printk(KERN_WARNING "can not alloc kbuf\n");
					ret = -ENOMEM;
					goto out;
				}
			}
write:
			ofs = sdev->fpga_info.chip_size * sdev->fpga_info.chip_count * flash + sdev->bbt_pos[flash] * sdev->fpga_info.block_size;
			ret = ssd_chip_erase(sdev, ofs, sdev->fpga_info.block_size);
			if(ret)
				goto out;

			cur_bbt = sdev->bbt + flash_bbt_len * flash;
			memset(kbuf, 0xff, sdev->fpga_info.page_size);
			memcpy(kbuf, cur_bbt, flash_bbt_len);
			ret = ssd_chip_write(sdev, kbuf, sdev->fpga_info.page_size, ofs);
			if(ret != sdev->fpga_info.page_size) {
				ssd_chip_erase(sdev, ofs, sdev->fpga_info.block_size);
				goto pick_another;
			} else {
				memset(kbuf, 0xff, sdev->fpga_info.page_size);
				flag_ofs = ofs + sdev->fpga_info.page_size;
				memcpy(kbuf, BBT_FLAG, BBT_FLAG_SIZE);
				ret = ssd_chip_write(sdev, kbuf, sdev->fpga_info.page_size, flag_ofs);
				if(ret != sdev->fpga_info.page_size) {
					ret =ssd_chip_erase(sdev, ofs, sdev->fpga_info.block_size);
					if(ret)
						goto out;
					goto pick_another;
				} else {
					ret = 0;
					continue;
				}
			}
pick_another:
			//mark cur block
			block = sdev->fpga_info.block_count * sdev->fpga_info.chip_count *flash + sdev->bbt_pos[flash];
			sdev->bbt[block >> 2] |= 0x01 << ((block & 0x03) << 1);

			for(i=sdev->bbt_pos[flash]+1, block++; i<BLOCK_RESERVED_FOR_BBT; i++, block++) {
				if(((sdev->bbt[block >> 2] >> ((block & 0x03) * 2) ) & 0x03) == 0x03) {
					sdev->bbt_pos[flash] = i;
					goto write;
				}
			}
			printk(KERN_ERR "No space left to write bad block table\n");
			ret = ENOSPC;
			goto out;
		}
	}

out:
	if(NULL != kbuf) kfree(kbuf);
	up(&sdev->bbt_sema);
	return ret;
}
#endif
#if 0
static int ssd_ram_read(struct ssd_device *sdev, void *buf, size_t length, loff_t ofs)
{
    dma_addr_t kbuf_dma_r;
    struct ssd_fpga_ram_op_msg *msg;
    int seq,ret = 0;
    uint32_t tmp;
    uint32_t timeout = 10;
uint64_t * tmp1 = (uint64_t *)buf;
//printk(KERN_WARNING "11111111111 in read fun : read_buf %llx w_buf %llx\n", tmp1[0], tmp1[1]);
    if(ofs >= SSD_MAX_FPGA_RAM_SIZE || ofs + length > SSD_MAX_FPGA_RAM_SIZE)
        return -EINVAL;

    if(!length)
	  return 0;    
   
    tmp = (uint32_t)(sdev->ioctl_flg >> 32);

    kbuf_dma_r = pci_map_single(sdev->pdev,buf, length, PCI_DMA_FROMDEVICE);
    if (dma_mapping_error(kbuf_dma_r)) {
        printk(KERN_WARNING "SSD:Unable to map  read DMA buffer.\n");
        ret = -ENOMEM;
	 goto out_map;
    }

     seq = ssd_get_ctrl_seq(sdev);
     msg = (struct ssd_fpga_ram_op_msg *)(sdev->dmatable_cpu[seq]);
     memset(msg,0xFF,sizeof(struct ssd_fpga_ram_op_msg));
     msg->msg_seq = seq;
     msg->fun = FUN_RAM_READ;
     msg->fpga_ram_addr = (uint32_t)ofs;
     msg->length = length;
     msg->buf_addr_low = (uint32_t)kbuf_dma_r;
     msg->buf_addr_high = (uint32_t)(kbuf_dma_r >> 32);

//printk(KERN_WARNING "2222222222222 in read fun : read_buf %llx w_buf %llx\n", tmp1[0], tmp1[1]);

//printk(KERN_WARNING "ssd_ram_read buf=%llx length=%d 111111111111111\n", buf,length);
     ssd_reg_write(sdev->ctrlp + REQ_FIFO_REG, sdev->dmatable_dma[seq]);

     //wait_event_interruptible(ssd_ioctl_wait, !test_bit(seq,&(sdev->ioctl_flg)));

//     printk("wait.................\n");
     timeout = wait_event_interruptible_timeout(ssd_ioctl_wait, !test_bit(seq,&(sdev->ioctl_flg)),timeout);
  //   printk("wake up now!!!!\n");

//	printk(KERN_WARNING "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! timeout=%d\n", timeout);
     if(!timeout)
	printk(KERN_WARNING "timeout\n");


    if(test_bit(seq,&tmp))
         ret = -EIO;		 
    	
    ssd_put_seq(sdev,seq);
    pci_unmap_single(sdev->pdev,kbuf_dma_r,length,PCI_DMA_FROMDEVICE);
	
//printk(KERN_WARNING "333333333333333 in read fun : read_buf %llx w_buf %llx\n", tmp1[0], tmp1[1]);
out_map:
     return ret == 0 ? length: ret;
}
#endif

static int ssd_ram_read(struct ssd_device *sdev, void *buf, size_t length, loff_t ofs)
{
    dma_addr_t kbuf_dma_r;
    struct ssd_fpga_ram_op_msg msg;
    int ret = 0;
    size_t len = length;
    loff_t ofs_w = ofs;

    if(ofs >= sdev->fpga_info.ramsize || ofs + length > sdev->fpga_info.ramsize
		|| length % RAM_WORD_LEN != 0 || ofs % RAM_WORD_LEN != 0) {
		printk(KERN_WARNING "param err\n");
        return -EINVAL;
	}

    if(!length) {
		printk(KERN_WARNING "ssd_ram_read length is 0\n");
		return 0;
	}

	len /= RAM_WORD_LEN;

	do_div(ofs_w, RAM_WORD_LEN);

    kbuf_dma_r = pci_map_single(sdev->pdev,buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_r)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif

		printk(KERN_WARNING "%s: Unable to map  read DMA buffer.\n", sdev->name);
		ret = -ENOMEM;
		goto out_map;
    }

	msg.fun = FUN_RAM_READ;
	msg.fpga_ram_addr = (uint32_t)ofs_w;
	msg.length = len;
	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);

	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_r,length,PCI_DMA_FROMDEVICE);

out_map:
     return ret == 0 ? length: ret;
}


static int ssd_ram_write(struct ssd_device *sdev, void * buf, size_t length, loff_t ofs)
{
    dma_addr_t kbuf_dma_w;
    struct ssd_fpga_ram_op_msg msg;
    int ret = 0;
    size_t len = length;
    loff_t ofs_w = ofs;

    if(ofs >= sdev->fpga_info.ramsize || ofs + length > sdev->fpga_info.ramsize
		|| length % RAM_WORD_LEN != 0 || ofs % RAM_WORD_LEN != 0) {
		printk(KERN_WARNING "param err\n");
        return -EINVAL;
	}

    if(!length) {
		printk(KERN_WARNING " 2 param err\n");
		return 0;    
    }

	len /= RAM_WORD_LEN;
	do_div(ofs_w, RAM_WORD_LEN);

    kbuf_dma_w = pci_map_single(sdev->pdev,buf, length, PCI_DMA_TODEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_w)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_w)) {
#endif

        printk(KERN_WARNING "%s: Unable to map  read DMA buffer.\n", sdev->name);
        ret = -ENOMEM;
		goto out_map;
    }

	msg.fun = FUN_RAM_WRITE;
	msg.fpga_ram_addr = (uint32_t)ofs_w;
	msg.length = len;
	msg.buf_addr_low = (uint32_t)kbuf_dma_w;
	msg.buf_addr_high = (uint32_t)((uint64_t)kbuf_dma_w >> 32);

	ret = ssd_do_direct_req(sdev, WRITE, (void*)&msg, NULL);

	pci_unmap_single(sdev->pdev,kbuf_dma_w,length,PCI_DMA_TODEVICE);

out_map:
     return ret == 0 ? length: ret;
     //return ret;

}

static int ssd_mark_bad(struct ssd_device *sdev, int flash, int block)
{
	int ret = 0;
	int ofs;
	uint64_t *bbt_word;
	uint64_t mark_bad;
	int len = sizeof(uint64_t);
	int ret_len = 0;

	if(flash > sdev->fpga_info.flash_count || block > sdev->fpga_info.block_count)
		return -EINVAL;

	bbt_word = (uint64_t*)kmalloc(sizeof(uint64_t), GFP_KERNEL);
	if(NULL == bbt_word) {
		printk(KERN_WARNING "can not alloc bbt_word\n");
		return -ENOMEM;
	}

	ofs = RAM_BBT_BASE_ADDR + RAM_BBT_SIZE * flash;
	ofs += (block / RAM_BBT_PER_WORD) * RAM_WORD_LEN;
	mark_bad =  ~(0x01LL << ((block % RAM_BBT_PER_WORD) * BBT_BITS_PERBLOCK));
	
	down(&sdev->bbt_sema);
	ret_len = ssd_ram_read(sdev, bbt_word, len, ofs);
	if(ret_len !=  len) {
		up(&sdev->bbt_sema);
		ret = -EIO;
		goto out;
	}

	*bbt_word &= mark_bad;
	ret_len = ssd_ram_write(sdev, bbt_word , len, ofs);
	if(ret_len !=  len) {
		up(&sdev->bbt_sema);
		ret = -EIO;
		goto out;
	}
	sdev->bbt_updated |= (0x1 << flash);
	up(&sdev->bbt_sema);

out:
	kfree(bbt_word);
	return 0;
}

//#define USE_RAM_BBT
static int ssd_read_bbt(struct ssd_device *sdev, int flash, uint8_t* bbt, size_t length, int mode)
{
	int ret = 0;
	uint8_t* kbuf = NULL;
//#ifndef USE_RAM_BBT
	int page = 0;
	int i;
//#else
	int ofs, ret_len;
//#endif

	if(NULL == bbt) {
		//printk(KERN_WARNING "bbt buf is null!\n");
		ret =  -EINVAL;
		goto err;
	}

	memset(bbt, 0xFF, length);

	kbuf = kmalloc((sdev->fpga_info.page_size*2), GFP_KERNEL);
	if(NULL == kbuf) {
		printk(KERN_WARNING "can not alloc kbuf\n");
		ret = -ENOMEM;
		goto err;
	}

	//printk(KERN_WARNING "sdev->fpga_info.chip_size = %llx %d, bbt_len=%d\n", sdev->fpga_info.chip_size, sdev->fpga_info.chip_count, flash_bbt_len);

		//printk("cur_flash=%d, ofs=0x%llx\n", cur_flash, cur_ofs);
	if(mode) {
		for(i=0; i<BLOCK_RESERVED_FOR_BBT; i++, page+=sdev->fpga_info.page_count) {
			ret = ssd_chip_read(sdev, kbuf, flash, 0, page+BBT_FLAG_PAGE, 1);
			if(unlikely(ret < 0 && ret != -EBADMSG)) {
				printk(KERN_WARNING "ssd_chip_read err ret =%d\n", ret);
				goto out_ioerr;
			}

			if(0 == memcmp(kbuf, BBT_FLAG, BBT_FLAG_SIZE)) {
				ret = ssd_chip_read(sdev, kbuf, flash, 0, page, BBT_FLAG_PAGE);
				if(unlikely(ret < 0 && ret != -EBADMSG)) {
					printk(KERN_WARNING "ssd_chip_read bbt err ret =%d\n", ret);
					goto out_ioerr;
				}
				break;
			}
		}
		if(i == BLOCK_RESERVED_FOR_BBT) {
			printk(KERN_WARNING "bbt not found for flash %d\n", flash);
			ret = ENOENT;
			goto out_notfound;
		}
	} else {
		ofs = RAM_BBT_BASE_ADDR + RAM_BBT_SIZE * flash;
		ret_len = ssd_ram_read(sdev, kbuf, length, ofs);
		if(ret_len != length) {
			printk(KERN_WARNING "read bbt from ddr error\n");
			goto out_ioerr;
		}
	}	
	memcpy(bbt, kbuf, length);

	//printk("BBT OK\n");
	kfree(kbuf);
	return ret;

#ifndef USE_RAM_BBT
out_notfound:
#endif

out_ioerr:
	kfree(kbuf);
err:
	return ret;

}


static int ssd_erase_bbt(struct ssd_device *sdev, int flash)
{
	int ret = 0;
	int page;
	uint8_t i;
	uint8_t* kbuf = NULL;

	kbuf = kmalloc(sdev->fpga_info.page_size, GFP_KERNEL);
	if(NULL == kbuf) {
		printk(KERN_WARNING "can not alloc kbuf\n");
		ret = -ENOMEM;
		goto err;
	}

	//printk(KERN_WARNING "sdev->fpga_info.chip_size = %llx %d, bbt_len=%d\n", sdev->fpga_info.chip_size, sdev->fpga_info.chip_count, flash_bbt_len);

	page = 0;
		//printk("cur_flash=%d, ofs=0x%llx\n", cur_flash, cur_ofs);
	for(i=0; i<BLOCK_RESERVED_FOR_BBT; i++,page+= sdev->fpga_info.page_count) {
		ret = ssd_chip_read(sdev, kbuf, flash, 0, page + BBT_FLAG_PAGE, 1);
		if(unlikely(ret < 0 && ret != -EBADMSG)) {
			printk(KERN_WARNING "ssd_chip_read err ret =%d\n", ret);
			goto out_ioerr;
		}

		if(0 == memcmp(kbuf, BBT_FLAG, BBT_FLAG_SIZE)) {
			ret = ssd_chip_erase(sdev, flash, 0, page);
			if(unlikely(ret != 0)) {
				printk(KERN_WARNING "ssd_chip_erase bbt err ret =%d\n", ret);
				goto out_ioerr;
			}
			break;
		}
	}
	if(i == BLOCK_RESERVED_FOR_BBT) {
		printk(KERN_WARNING "bbt not found for flash %d\n", flash);
		ret = -ENOENT;
		goto out_notfound;
	}

	//printk("BBT OK\n");
	kfree(kbuf);
	return ret;

out_notfound:
out_ioerr:
	kfree(kbuf);
err:
	return ret;

}

static int ssd_ram_9step(struct ssd_device *sdev)
{
	int ret = 0;
	uint64_t* kbuf = NULL;
	uint8_t* w_buf = NULL;
	uint8_t* pre_w_buf = NULL;
	int32_t ofs;
	int32_t len;

	kbuf = (uint64_t*)kmalloc(3*RAM_9STEP_YARD, GFP_KERNEL);
	if(NULL == kbuf) {
		printk(KERN_WARNING "can not alloc kbuf\n");
		ret = -ENOMEM;
		goto out;
	}

	w_buf = (uint8_t *)(kbuf + RAM_9STEP_YARD/sizeof(uint64_t));
	pre_w_buf = (uint8_t *)(kbuf + 2*(RAM_9STEP_YARD/sizeof(uint64_t)));

	//w0
	//printk(KERN_WARNING "w0\n");
	memset((void *)kbuf, 0, 3*RAM_9STEP_YARD);
	for(ofs=0; ofs<sdev->fpga_info.ramsize; ofs+=RAM_9STEP_YARD) {
		len = ssd_ram_write(sdev, (void *)kbuf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_write err\n");
			ret = -EIO;
			goto out_free;
		}
	}

	//r0w1
	//printk(KERN_WARNING "r0w1\n");
	memset(w_buf, 0xFF, RAM_9STEP_YARD);
	for(ofs=0; ofs<sdev->fpga_info.ramsize; ofs+=RAM_9STEP_YARD) {
		len = ssd_ram_read(sdev, (void *)kbuf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_read err\n");
			ret = -EIO;
			goto out_free;
		}

		if(0 != memcmp(pre_w_buf, (void *)kbuf, RAM_9STEP_YARD)) {
			printk(KERN_WARNING "fpga ram err\n");
			ret = -EBADMSG;
			goto out_free;
		}

		len = ssd_ram_write(sdev, w_buf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_write err\n");
			ret = -EIO;
			goto out_free;
		}
	}
	memset(pre_w_buf, 0xFF, RAM_9STEP_YARD);

	//r1w0
	//printk(KERN_WARNING "r1w0\n");
	memset(w_buf, 0x00, RAM_9STEP_YARD);
	for(ofs=0; ofs<sdev->fpga_info.ramsize; ofs+=RAM_9STEP_YARD) {
		len = ssd_ram_read(sdev, (void *)kbuf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_read err\n");
			ret = -EIO;
			goto out_free;
		}

		if(0 != memcmp(pre_w_buf, (void *)kbuf, RAM_9STEP_YARD)) {
			printk(KERN_WARNING "fpga ram err\n");
			ret = -EBADMSG;
			goto out_free;
		}

		len = ssd_ram_write(sdev, w_buf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_write err\n");
			ret = -EIO;
			goto out_free;
		}
	}
	memset(pre_w_buf, 0x00, RAM_9STEP_YARD);

	//r0w1
	//printk(KERN_WARNING "r0w1\n");
	memset(w_buf, 0xFF, RAM_9STEP_YARD);
	for(ofs=sdev->fpga_info.ramsize-RAM_9STEP_YARD; ofs>=0; ofs-=RAM_9STEP_YARD) {
		len = ssd_ram_read(sdev, (void *)kbuf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_read err\n");
			ret = -EIO;
			goto out_free;
		}

		if(0 != memcmp(pre_w_buf, (void *)kbuf, RAM_9STEP_YARD)) {
			printk(KERN_WARNING "fpga ram err\n");
			ret = -EBADMSG;
			goto out_free;
		}

		len = ssd_ram_write(sdev, w_buf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_write err\n");
			ret = -EIO;
			goto out_free;
		}
	}
	memset(pre_w_buf, 0xFF, RAM_9STEP_YARD);

	//r1w0
	//printk(KERN_WARNING "r1w0\n");
	memset(w_buf, 0x00, RAM_9STEP_YARD);
	for(ofs=sdev->fpga_info.ramsize-RAM_9STEP_YARD; ofs>=0; ofs-=RAM_9STEP_YARD) {
		len = ssd_ram_read(sdev, (void *)kbuf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_read err\n");
			ret = -EIO;
			goto out_free;
		}

		if(0 != memcmp(pre_w_buf, (void *)kbuf, RAM_9STEP_YARD)) {
			printk(KERN_WARNING "fpga ram err\n");
			ret = -EBADMSG;
			goto out_free;
		}

		len = ssd_ram_write(sdev, w_buf, RAM_9STEP_YARD, ofs);
		if(unlikely(RAM_9STEP_YARD != len)) {
			printk(KERN_WARNING "ssd_ram_write err\n");
			ret = -EIO;
			goto out_free;
		}
	}
	memset(pre_w_buf, 0x00, RAM_9STEP_YARD);

out_free:
	kfree(kbuf);
out:
	return ret;
}

#define TEST_ADDR_A	(1024*177)
#define TEST_ADDR_B	(1024*1024*57)
static int ssd_ram_3step(struct ssd_device *sdev)
{
	int ret = 0;
	uint64_t* kbuf = NULL;
	uint64_t* w_buf = NULL;
	//uint32_t ofs;
	uint32_t len;
	int i;
	int round;

	kbuf = (uint64_t*)kmalloc(16, GFP_KERNEL);
	if(NULL == kbuf) {
		printk(KERN_WARNING "can not alloc kbuf\n");
		ret = -ENOMEM;
		goto out;
	}
	w_buf = (kbuf + 1);

	//step1
	kbuf[0] = 0x0;
	kbuf[1] = 0xFFFFFFFFFFFFFFFFULL;
	len = ssd_ram_write(sdev, (void *)kbuf, 8, TEST_ADDR_A);
	if(unlikely(8 != len)) {
		printk(KERN_WARNING "ssd_ram_write err\n");
		ret = -EIO;
		goto out_free;
	}

	len = ssd_ram_write(sdev, (void *)w_buf, 8, TEST_ADDR_B);
	if(unlikely(8 != len)) {
		printk(KERN_WARNING "ssd_ram_write err\n");
		ret = -EIO;
		goto out_free;
	}

	len = ssd_ram_read(sdev, (void *)kbuf, 8, TEST_ADDR_A);
	if(unlikely(8 != len)) {
		printk(KERN_WARNING "ssd_ram_read err\n");
		ret = -EIO;
		goto out_free;
	}

	if(unlikely(0 != kbuf[0])) {
		printk(KERN_WARNING "fpga ram err: step1 addr a\n");
		ret = -EBADMSG;
		goto out_free;
	}

	len = ssd_ram_read(sdev, (void *)kbuf, 8, TEST_ADDR_B);
	if(unlikely(8 != len)) {
		printk(KERN_WARNING "ssd_ram_read err\n");
		ret = -EIO;
		goto out_free;
	}

	if(unlikely(0xFFFFFFFFFFFFFFFFULL != kbuf[0])) {
		printk(KERN_WARNING "fpga ram err: step1 addr b\n");
		ret = -EBADMSG;
		goto out_free;
	}

	//step2
	for(round=0;round<2;round++) {
		for(i=0;i<64;i++) {
			*w_buf = 0x01 << i;
			if(round) {
				*w_buf = ~(*w_buf);
				*w_buf &= 0x3FFFFFF;
			}

			len = ssd_ram_write(sdev, (void *)w_buf, 8, TEST_ADDR_A);
			if(unlikely(8 != len)) {
				printk(KERN_WARNING "ssd_ram_write err\n");
				ret = -EIO;
				goto out_free;
			}

			len = ssd_ram_read(sdev, (void *)kbuf, 8, TEST_ADDR_A);
			if(unlikely(8 != len)) {
				printk(KERN_WARNING "ssd_ram_read err\n");
				ret = -EIO;
				goto out_free;
			}

			if(unlikely(*w_buf != kbuf[0])) {
				printk(KERN_WARNING "fpga ram err: step2 addr a\n");
				ret = -EBADMSG;
				goto out_free;
			}
		}
	}

	//step3
	for(round=0;round<2;round++) {
		for(i=0;i<26;i++) {
			*w_buf = 0x01 << i;
			if(round) {
				*w_buf = ~(*w_buf);
				*w_buf &= 0x3FFFFFF;
			}
			if((*w_buf & 0x0F) !=0) continue;
			if(*w_buf > sdev->fpga_info.ramsize) break;

			len = ssd_ram_write(sdev, (void *)w_buf, 8, *w_buf);
			if(unlikely(8 != len)) {
				printk(KERN_WARNING "ssd_ram_write err\n");
				ret = -EIO;
				goto out_free;
			}

			len = ssd_ram_read(sdev, (void *)kbuf, 8, *w_buf);
			if(unlikely(8 != len)) {
				printk(KERN_WARNING "ssd_ram_read err\n");
				ret = -EIO;
				goto out_free;
			}

			len = ssd_ram_read(sdev, (void *)kbuf, 8, *w_buf);
			if(unlikely(*w_buf != kbuf[0])) {
				printk(KERN_WARNING "fpga ram err: step3\n");
				ret = -EBADMSG;
				goto out_free;
			}

		}

		for(i=0;i<26;i++) {
			*w_buf = 0x01 << i;
			if(round) {
				*w_buf = ~(*w_buf);
				*w_buf &= 0x3FFFFFF;
			}
			if((*w_buf & 0x0F) !=0) 
				continue;
			
			if(*w_buf > sdev->fpga_info.ramsize) 
				break;

			len = ssd_ram_read(sdev, (void *)kbuf, 8, *w_buf);
			if(unlikely(8 != len)) {
				printk(KERN_WARNING "ssd_ram_read err\n");
				ret = -EIO;
				goto out_free;
			}

			len = ssd_ram_read(sdev, (void *)kbuf, 8, *w_buf);
			if(unlikely(*w_buf != kbuf[0])) {
				printk(KERN_WARNING "fpga ram err: step3\n");
				ret = -EBADMSG;
				goto out_free;
			}
		}
	}

out_free:
	kfree(kbuf);
out:
	return ret;
}

#define LOG_USE_PRINTK
#ifdef LOG_USE_PRINTK
char *evt_msg[16] = {
    "Create block BBT failed!",
    "Errors occurred while read BBT!",
    "Mark as a bad block which erased failed!",
    "Flush block BBT failed!",
    "Write failed and try to write to another block!",
    "Write failed and give the writing up!",
    "Cache read failed while writing!",
    "No available blocks!",
    "Read EC failed while initializing!",
    "Read VID failed while initializing!",
    "Implement WL on the logic block!",
    "Read data back failed in WL on the physical block!",
    "WL failed while writing!",
    "Read redundant page map error!",
    "BCH error!",
    "Mark it bad which programmed failed!",
};

enum event_type {
	EVT_CREATE_BBT = 0x00,
	EVT_READ_BBT = 0x01,
	EVT_ERASE_OVERLOAD = 0x02,
	EVT_FLUSH_BBT = 0x03,
	EVT_WRITE_RENEW = 0x04,
	EVT_WRITE_FAILED = 0x05,
	EVT_W_CACHE_FAIL = 0x06,
	EVT_NO_AVAIL_BLOCK = 0x07,
	EVT_READ_EC_FAIL = 0x08,
	EVT_READ_VID_FAIL = 0x09,
	EVT_EXEC_WL = 0x0a,
	EVT_WL_READ_BACK_FAIL = 0x0b,
	EVT_WL_WRITE_FAIL = 0x0c,
	EVT_RPM_ERR = 0x0d,
	EVT_READ_ERR = 0x0e,
	EVT_MARK_BAD = 0x0f,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_get_log(void *data)	
{
	struct ssd_device *sdev = (struct ssd_device *)data;
#else
static void ssd_get_log(struct work_struct *work)
{
	struct ssd_device *sdev = container_of(work, struct ssd_device, read_wk);
#endif
	struct ssd_log log_infor;
	struct ssd_log_msg msg;
	int i = 0;
	int msg_count, ret = 0, retry = 0;
	dma_addr_t kbuf_dma_r;
	size_t length = sizeof(uint64_t)*SSD_MAX_LOG;
	uint64_t* log_buf;

	/* fixed me: if get the log from DMA buff, we should protect the local buff with mutex */
	down(&log_mutex);
	memset(&msg, 0, sizeof(struct ssd_log_msg));

	log_buf = kmalloc(length, GFP_KERNEL);
	if(!log_buf) {
		printk(KERN_ERR "%s: system no memory\n", sdev->name);
		return;
	}

	kbuf_dma_r = pci_map_single(sdev->pdev, log_buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_r)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif

		printk(KERN_WARNING "%s: unable to map log DMA buffer.\n", sdev->name);
		kfree(log_buf);
		return;
	}

	msg.log_count = SSD_MAX_LOG;
	msg.fun = FUN_GET_LOG;
	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_add_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);
log_again:
	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, &msg_count);
	if(ret && retry < 2) {
		retry++;
		goto log_again;
	} else if(ret) {
		pci_unmap_single(sdev->pdev, kbuf_dma_r, length, PCI_DMA_FROMDEVICE); 
		kfree(log_buf);
		return;
	}
		
	pci_unmap_single(sdev->pdev, kbuf_dma_r, length, PCI_DMA_FROMDEVICE); 

	//parse data
	for(i=0; i < msg_count; i++) {
		memset(&log_infor, 0, sizeof(struct ssd_log));
		log_infor.log_seq= (uint16_t)((*(log_buf+i) & 0xffff000000000000ULL)>>48);
		log_infor.event_type = (uint16_t)((*(log_buf+i) & 0xffff00000000ULL)>>32);
		log_infor.chip = (uint8_t)((*(log_buf+i) & 0xff000000ULL) >> 24);
		log_infor.block = (uint16_t)((*(log_buf+i) & 0xffff00ULL)>>8);
		log_infor.page = (uint8_t)(*(log_buf+i));

		if(log_infor.event_type > EVT_MARK_BAD) {
			printk(KERN_ERR "ssd_get_log: get a exceptional log type and will give it up.\n");
			continue;
		}
		if(log_infor.event_type != EVT_WRITE_FAILED &&
			log_infor.event_type != EVT_W_CACHE_FAIL &&
			log_infor.event_type != EVT_NO_AVAIL_BLOCK &&
			log_infor.event_type != EVT_READ_ERR)
			continue;
		
		printk(KERN_INFO "/dev/ssd%c: <%d> chip_no %d block_no %d  page_no %d: %s\n", \
			sdev->controller_idx + 'a', log_infor.event_type, log_infor.chip, \
			log_infor.block, log_infor.page, evt_msg[log_infor.event_type]); 
		
	}

	if(log_buf)
		kfree(log_buf);

	up(&log_mutex);

	return;
}
#else
#define FILE_NAME_LEN 256
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_get_log(void *data)	
{
	struct ssd_device *sdev = (struct ssd_device *)data;
#else
static void ssd_get_log(struct work_struct *work)
{
	struct ssd_device *sdev = container_of(work, struct ssd_device, read_wk);
#endif
	int flag = 0, m = 0, n = 0;
	char log_helper[FILE_NAME_LEN] = "/sbin/ssd_logd";
	char *argv [3];
	static int log_initial = 0;
	static char *envp[] = { "HOME=/",
				"TERM=linux",
				"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
				NULL };
	argv[0] = log_helper;
	argv[1] = NULL;
	argv[2] = NULL;

	/*because kernel have blocked all the signals, we need not do that*/
wait:
	if (not_deal_log == 0) {
		if (log_initial == 0) {
			if(call_usermodehelper (argv[0], argv, envp, 0) && flag++ < 20) {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(3000);
				goto wait;
			}
			if (flag == 20)
				return;
			log_initial = 1;
			while(!log_pid && n < 5) {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(2000);
				n++;
			}
			n = 0;
		}
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (log_pid == 0 || kill_proc(log_pid, 0, 1) == -ESRCH) {
#else
		if (log_pid == 0 || kill_pid(find_vpid(log_pid), 0, 1) == -ESRCH) {
#endif
			log_initial = 0;
			log_pid = 0;
			m++;
			if (6 == m)
				return;
			goto wait;
		}
		if (log_pid != 0)
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
			kill_proc(log_pid, SSD_SIGLOG - sdev->controller_idx, 22);
#else
			kill_pid(find_vpid(log_pid), SSD_SIGLOG - sdev->controller_idx, 22);
#endif
	}
	return;
}
#endif

static int ssd_put_log(void *data, struct ssd_log_info_user *info)
{
	struct ssd_device *sdev = (struct ssd_device *)data;
	struct ssd_log_msg msg;
	struct timeval t;
	int msg_count, ret = 0, retry = 0;
	dma_addr_t kbuf_dma_r;
	size_t length = sizeof(uint64_t)*SSD_MAX_LOG;
	void* log_buf;
	
	memset(&msg, 0, sizeof(struct ssd_log_msg));

	log_buf = kmalloc(length, GFP_KERNEL);
	if(!log_buf) {
		printk(KERN_ERR "%s: system no memory\n", sdev->name);
		return -EFAULT;
	}

	kbuf_dma_r = pci_map_single(sdev->pdev, log_buf, length, PCI_DMA_FROMDEVICE);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		if (dma_mapping_error(kbuf_dma_r)) {
#else
		if (dma_mapping_error(&(sdev->pdev->dev), kbuf_dma_r)) {
#endif

		printk(KERN_WARNING "%s: unable to map log DMA buffer.\n", sdev->name);
		kfree(log_buf);
		return -EFAULT;
	}

	msg.log_count = SSD_MAX_LOG;
	msg.fun = FUN_GET_LOG;
	msg.buf_addr_low = (uint32_t)kbuf_dma_r;
	msg.buf_add_high = (uint32_t)((uint64_t)kbuf_dma_r >> 32);
log_again:
	ret = ssd_do_direct_req(sdev, READ, (void*)&msg, &msg_count);
	if(ret && retry < 2) {
		retry++;
		goto log_again;
	} else if(ret) {
		pci_unmap_single(sdev->pdev, kbuf_dma_r, length, PCI_DMA_FROMDEVICE); 
		kfree(log_buf);
		return -EFAULT;
	}
		
	pci_unmap_single(sdev->pdev, kbuf_dma_r, length, PCI_DMA_FROMDEVICE); 

	memset(&t, 0, sizeof(struct timeval));
	do_gettimeofday(&t);
	memcpy(&(info->time), &t, sizeof(struct timeval));

	//get data
	info->count = msg_count;
	info->idx = sdev->controller_idx;
	memcpy(info->buf, log_buf, sizeof(uint64_t)*msg_count);
		
	if(log_buf)
		kfree(log_buf);

	return 0;
}

#if 0
static inline int ssd_handle_rw_resp(struct ssd_device *sdev,struct ssd_resp_msg *msg,int idx)
{
	unsigned long flags; 

	spin_lock_irqsave(&sdev->map_lock,flags);
	sdev->rq_flg |= 1ULL << idx;
	spin_unlock_irqrestore(&sdev->map_lock,flags);

	if(msg->status) {
		printk(KERN_ERR"SSD:msg %d status return err %d\n",msg->msg_seq,msg->status);

		spin_lock_irqsave(&sdev->map_lock,flags);
		sdev->rq_flg |= 1ULL << (idx+SSD_MAX_RESP_Q_LEN);
		spin_unlock_irqrestore(&sdev->map_lock,flags);
	}

	memset(msg,0xFF,sizeof(struct ssd_resp_msg)); 

	wake_up(&sdev->wait_q);

	return 0;
}
#endif

static int ssd_flush(struct ssd_device *sdev)
{
	int ret = 0;
	struct ssd_flush_cache_or_bbt_msg msg;

	//memset(msg,0,sizeof(struct ssd_flush_cache_msg));

	if(unlikely(mode == SSD_DRV_MODE_DEBUG))
		return 0;

	msg.fun = FUN_FLUSH_CACHE_OR_BBT;
	msg.msg_flg = 0x0;

	msg.flash = 0;
	ret = ssd_do_direct_req(sdev, WRITE, (void*)&msg, NULL);


	return ret;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static int ssd_issue_flush_fn(struct request_queue *q, struct gendisk *disk,
			       sector_t *error_sector)
{
	struct ssd_device *sdev = q->queuedata;

	return ssd_flush(sdev);
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static void ssd_flush_work(void *data)
{
	struct ssd_device *sdev = (struct ssd_device *)data;
#else
static void ssd_flush_work(struct work_struct *work)
{
	struct ssd_device *sdev = container_of(work, struct ssd_device, flush_wk);
#endif

	ssd_flush(sdev);
	return;
}

static void ssd_flush_timer(unsigned long data)
{
	struct ssd_device *sdev = (struct ssd_device *) data;

	queue_work(sdev->workqueue, &sdev->flush_wk);
}


static int ssd_flush_bbt(struct ssd_device *sdev, int flash)
{
	int ret = 0;
	struct ssd_flush_cache_or_bbt_msg msg;

	if(unlikely(flash > sdev->fpga_info.flash_count))
		return -EINVAL;
	//memset(msg,0,sizeof(struct ssd_flush_cache_msg));

	msg.fun = FUN_FLUSH_CACHE_OR_BBT;
	msg.msg_flg = 0x1;
	msg.flash = flash;

	down(&sdev->bbt_sema);
	ret = ssd_do_direct_req(sdev, WRITE, (void*)&msg, NULL);
	if(!ret)
		sdev->bbt_updated &= ~(0x1<<flash);
	up(&sdev->bbt_sema);

	return ret;
}

static int ssd_flush_allbbt(struct ssd_device *sdev)
{
	int ret = 0;
	int i;
	int flush_ok = 0;

	for(i=0; i<sdev->fpga_info.flash_count; i++) {
		if((sdev->bbt_updated>>i) & 0x1) {
			ret = ssd_flush_bbt(sdev, i);
			if(ret) {
				printk(KERN_WARNING "%s: update flash %d bbt error\n", sdev->name, i);
				break;
			}
			flush_ok++;
		}
	}
	return (ret ? ret: flush_ok);
}


void ssd_add_timer(struct ssd_cmnd *cmd, int timeout, void (*complete)(struct ssd_cmnd *))
{
	/*
	 * If the clock was already running for this command, then
	 * first delete the timer.  The timer handling code gets rather
	 * confused if we don't do this.
	 */
	if (cmd->eh_timeout.function)
		del_timer(&cmd->eh_timeout);

	cmd->eh_timeout.data = (unsigned long)cmd;
	cmd->eh_timeout.expires = jiffies + timeout;
	cmd->eh_timeout.function = (void (*)(unsigned long)) complete;

	add_timer(&cmd->eh_timeout);
}

int ssd_delete_timer(struct ssd_cmnd *cmd)
{
	int rtn = 1;

	if (cmd->eh_timeout.function) {
		rtn = del_timer(&cmd->eh_timeout);

		cmd->eh_timeout.data = (unsigned long)NULL;
		cmd->eh_timeout.function = NULL;
	}

	return rtn;
}


static inline void __ssd_done(struct ssd_cmnd* cmd)
{
	unsigned long flags;

	local_irq_save(flags);
	list_add_tail(&cmd->list, &get_cpu_var(ssd_done_q));
	tasklet_hi_schedule(&get_cpu_var(ssd_tasklet));
	local_irq_restore(flags);
}

static inline void ssd_done(struct ssd_cmnd* cmd)
{
	if (!ssd_delete_timer(cmd))
		return;

	__ssd_done(cmd);
}

void ssd_times_out(struct ssd_cmnd *cmd)
{
	struct ssd_device *sdev = cmd->device;

	printk(KERN_WARNING "%s: cmd timeout\n", sdev->name);
	cmd->errors = -ETIMEDOUT;
	__ssd_done(cmd);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
static irqreturn_t ssd_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
#else
static irqreturn_t ssd_interrupt(int irq, void *dev_instance)

#endif
{
	struct ssd_device *sdev = (struct ssd_device *)dev_instance;
	struct request *req;
	struct ssd_cmnd *cmd;
	ssd_response_msq_t * msg = NULL;
	int i;
	uint64_t cur_resp_idx;
	uint64_t tmp_idx,count;
	uint8_t status;
	uint8_t notify = 0;

	cur_resp_idx = *(uint64_t *)sdev->resp_point_addr;

	if(sdev->resp_idx  == cur_resp_idx )
		return IRQ_RETVAL(IRQ_NONE);


	tmp_idx = (sdev->resp_idx  + 1) % SSD_MAX_RESP_Q_LEN;

	if(cur_resp_idx > sdev->resp_idx)
		count = cur_resp_idx - sdev->resp_idx;
	else
		count = SSD_MAX_RESP_Q_LEN - (sdev->resp_idx & SSD_MSG_MASK) + cur_resp_idx;

	if(unlikely(count >  SSD_MAX_RESP_Q_LEN)) {
//		printk("ssd: handle_responses sdev->resp_idx=%ld, count = %d, idx=%d\n",  sdev->resp_idx, count, idx);
		//ssd_reg32_write(sdev->ctrlp + SSD_CLEAR_INTR_REG,1); 
		printk(KERN_WARNING "%s: too many response cmd count %lld\n", sdev->name, count);
		return IRQ_RETVAL(IRQ_NONE);
	}

	for(i = 0;i < count ;i++,tmp_idx = ((tmp_idx + 1) % SSD_MAX_RESP_Q_LEN)) {
		msg = (ssd_response_msq_t *)(&sdev->resp_stat_addr_base[tmp_idx]);
		if(unlikely(msg->uint_msg == SSD_DEFAULT_MSG)) {
			printk(KERN_WARNING "%s: error resp msg idx = %lld\n", sdev->name, tmp_idx); 
			continue;
		}

		if(likely(msg->resp_msg.fun != FUN_GET_LOG)) {
			status = msg->resp_msg.status;
			notify = msg->resp_msg.notify;
		} else {
			status = msg->logresp_msg.stat;
		}

		if(unlikely(status && mode != SSD_DRV_MODE_DEBUG)) {
			printk(KERN_WARNING "%s: I/O error %d\n", sdev->name, status);
#ifdef SHOW_BITFLIP
			if(msg->resp_msg.fun == FUN_READ || 
				msg->resp_msg.fun == FUN_NAND_CHIP_READ_WITH_OOB ||
				msg->resp_msg.fun == FUN_NAND_CHIP_READ) {
				sdev->bitflip[0]++;
				sdev->read_amount++;
			}
#endif
		}

		req = blk_queue_find_tag(sdev->queue, msg->resp_msg.msg_seq);
		if(unlikely(!req)) {
			printk(KERN_WARNING "%s: unknown tag %d\n", sdev->name, msg->resp_msg.msg_seq);
			continue;
		}
		cmd = (struct ssd_cmnd *)req->special;
		cmd->errors = status & status_mask;
		cmd->log_count = msg->logresp_msg.record_count;
		
		ssd_done(cmd);

#ifdef SHOW_BITFLIP
		if(status == 0 && (msg->resp_msg.fun == FUN_READ || 
				msg->resp_msg.fun == FUN_NAND_CHIP_READ_WITH_OOB ||
				msg->resp_msg.fun == FUN_NAND_CHIP_READ)) {
				if(unlikely(msg->resp_msg.bit_flip > 0))
					sdev->bitflip[msg->resp_msg.bit_flip]++;
				sdev->read_amount++;
		}
#endif

		if(notify)
			queue_work(sdev->workqueue, &sdev->read_wk);
		//	schedule_work(&sdev->read_wk);

		msg->uint_msg = SSD_DEFAULT_MSG;
	}
	sdev->resp_idx = cur_resp_idx;
	ssd_reg32_write(sdev->ctrlp + SSD_CLEAR_INTR_REG,1); 

	return IRQ_RETVAL(IRQ_HANDLED);
}

static inline int ssd_dispatch_cmd(struct ssd_cmnd *cmd)
{
	struct ssd_device *sdev = cmd->device;
	struct request *req = cmd->request;
	struct ssd_rw_msg *msg;
	int tag = req->tag;

	if(unlikely(sdev == NULL || req == NULL))
		return -EINVAL;

	if(unlikely(tag >= SSD_MAX_TAGS || tag < 0)) {
		printk(KERN_ERR "%s: wrong tag value %d at ctrl %d\n", 
			sdev->name, tag, sdev->controller_idx);
		return -EINVAL;
	}

	msg = (struct ssd_rw_msg *)(sdev->dmatable_cpu[tag]);
	msg->msg_seq = tag;

	ssd_add_timer(cmd, SSD_CMD_TIMEOUT, ssd_times_out);
	//init_timer(&sdev->cmd_timeout[tag]);
	//ssd_add_timer(sdev, req, SSD_CMD_TIMEOUT, ssd_times_out);

#if 0
	//	printk(KERN_WARNING "seq=%d, tag=%d\n", msg->msg_seq, tag);
	{ ////yj
		int i;
		printk("ssd: do_cmd  fun = %d, sg_num = %d, msg_seq = %d\n",msg->fun, msg->sg_num, msg->msg_seq);
		for (i = 0; i< msg->sg_num;i++) {
			printk("block_low = %x, block_high = %x, length = %x, addr_low = %x, add_high = %x\n",
				msg->ssd_sg_entry[i].block_start_low,msg->ssd_sg_entry[i].block_start_high,
				msg->ssd_sg_entry[i].length, msg->ssd_sg_entry[i].buf_addr_low, 
				msg->ssd_sg_entry[i].buf_addr_high);
		}
	}
#endif
	ssd_reg_write(sdev->ctrlp + REQ_FIFO_REG,sdev->dmatable_dma[tag]);

	return 0;

}

static int ssd_init_responses(struct ssd_device  *sdev)
{
	sdev->resp_stat_addr_base = pci_alloc_consistent(sdev->pdev,(sizeof(struct ssd_resp_msg))*SSD_MAX_RESP_Q_LEN,&(sdev->resp_stat_dma_addr));
	if (!sdev->resp_stat_addr_base) {
		printk(KERN_WARNING "%s: unable to allocate SSD resp stat DMA buffer.\n", sdev->name);
		goto err_no_mem1;
	}

	memset(sdev->resp_stat_addr_base,0xFF,(sizeof(struct ssd_resp_msg))*SSD_MAX_RESP_Q_LEN);
	//memset(sdev->resp_point_addr,0xFF,32);
	sdev->resp_point_addr = pci_alloc_consistent(sdev->pdev,8,&(sdev->resp_point_dma_addr));
	if(!sdev->resp_point_addr){
		printk(KERN_WARNING "%s: unable to allocate SSD resp point DMA buffer.\n", sdev->name);
		goto err_no_mem2;
	}
	*(uint64_t *)(sdev->resp_point_addr) = SSD_DEFAULT_RESP_IDX;
	
	//sdev->resp_idx = 0xFF;
	sdev->resp_idx = SSD_DEFAULT_RESP_IDX;

	ssd_reg_write(sdev->ctrlp+RESP_FIFO_REG,sdev->resp_stat_dma_addr);
	ssd_reg_write(sdev->ctrlp+RESP_FIFO_P_REG,sdev->resp_point_dma_addr);      

	return 0;
err_no_mem2:
	pci_free_consistent(sdev->pdev,sizeof(struct ssd_resp_msg)*SSD_MAX_RESP_Q_LEN,sdev->resp_stat_addr_base,sdev->resp_stat_dma_addr);
err_no_mem1:
	return  -ENOMEM;

}

static int ssd_init_msg(struct ssd_device  *sdev)
{
    int i;

    for(i = 0;i < SSD_CMD_FIFO_DEPTH ;i++) {
        sdev->dmatable_cpu[i] = pci_alloc_consistent(sdev->pdev, sizeof(struct ssd_rw_msg),&(sdev->dmatable_dma[i]));
        if(!(sdev->dmatable_cpu[i])) {
			printk(KERN_ERR "%s: can't alloc dmatable for rw msg!\n", sdev->name);
			goto nomem;
        }
    }

    return 0;

nomem:
    for(--i;i >=0;i--)
		pci_free_consistent(sdev->pdev,sizeof(struct ssd_rw_msg),sdev->dmatable_cpu[i],sdev->dmatable_dma[i]);
    return  -ENOMEM;
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define SSD_PROC_DIR "ssd_adv"

static struct proc_dir_entry *proc_ssd;

static struct proc_dir_entry *proc_info;

static int proc_show_info(struct seq_file *m, void *data)
{
	int i, c, size;
	struct ssd_device *dev = NULL;
	struct ssd_device *itor;
	char type;

	seq_printf(m, "Driver             Version:\t%s\n", DRIVER_VERSION); 
	seq_printf(m, "Supported   Controller VER:\t%03X to %03X\n", FPGA_VERSION_MIN, FPGA_VERSION_MAX); 

	for(i = 0;i < MAX_CARD; i++, dev=NULL) {
		if(ssd_card_table[i]->bus != 0) {
			c=i+1;
				
			size = (int)((ssd_card_table[i]->controller_size>>30) * 
								ssd_card_table[i]->expect_dev_count);
			if(ssd_card_table[i]->type == SSD_TYPE_MLC) //MLC
				type='2';
			else if(ssd_card_table[i]->type == SSD_TYPE_SLC) //SLC
				type='0';
			else
				type='!';

			seq_printf(m, "\nSSD %d                 Name:\tES1%c01-%d\n", c, type, size);
			seq_printf(m, "SSD %d                 Size:\t%dGB\n", c, size);
			seq_printf(m, "SSD %d                 Type:\t%s\n", c, type=='2'? "MLC":(type=='0'?"SLC":"unknown"));
			seq_printf(m, "SSD %d       Controller VER:\t%03X\n", c, ssd_card_table[i]->version); 
			seq_printf(m, "SSD %d             CPLD VER:\t%03X\n", c, ssd_card_table[i]->cpld_ver);
			seq_printf(m, "SSD %d             PCB  VER:\t.%c\n", c, (ssd_card_table[i]->pcb_ver+'A'-1));
			if(ssd_card_table[i]->upper_pcb_ver != 0)
				seq_printf(m, "SSD %d        Upper PCB VER:\t.%c\n", c, (ssd_card_table[i]->upper_pcb_ver+'A'-1));

			seq_printf(m, "SSD %d Internal Stripe size:\t", c);
			if(ssd_card_table[i]->stripe_size == SSD_STRIPE_4K)
				seq_printf(m, "4K\n");
			else if(ssd_card_table[i]->stripe_size == SSD_STRIPE_64K)
				seq_printf(m, "64K\n");
			else
				seq_printf(m, "unknown\n");

			seq_printf(m, "SSD %d      Reserved Blocks:\t%d%%\n", c, ssd_card_table[i]->rb_percent);
			seq_printf(m, "SSD %d              Devices:\t", c);
			list_for_each_entry(itor, &(ssd_card_table[i]->ssd_list), list) {
				seq_printf(m, "%s ", itor->name);
			}
			if(ssd_card_table[i]->dev_count == ssd_card_table[i]->expect_dev_count) {
				seq_printf(m, "\n" );
			} else {
				seq_printf(m, "(some missed)\n" );
			}
		}
	}
	
	return 0;
}

static int proc_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_show_info, PDE_DATA(inode));
}

static const struct file_operations proc_info_fops = {
	.open           = proc_info_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};
#endif /* CONFIG_PROC_FS */
#if 0
static int __init ssd_get_fpga_stat(struct ssd_device *sdev)
{
	int ret = CHIP_INITING,i,ii;
	uint8_t reg[4][8];
	uint64_t tmp;

	memset(reg,CHIP_INITING,sizeof(reg));

	for(i = 0;i < 4;i++) {
		tmp = ssd_reg_read(sdev->ctrlp + FPGA_STATUS_REG);
		memcpy(reg[i],&tmp,sizeof(uint64_t));
	}

	for(i =0;i < 4;i++ ) {
		for(ii = 0;ii < 8;ii++) {
			switch(ret = reg[i][ii]) {
				case FPGA_INIT_SUCCESS:
					break;
				case CHIP_INITING:
					printk(KERN_INFO"SSD: Flash %d initing now. Please wait a moment,and try it again\n",i*ii);
					goto out;
				case CHIP_ID_ERR:
					printk(KERN_ERR"SSD: Flash %d CHIP ID ERR\n",i*ii);
					goto out;
				case WRITE_BBT_ERR:
					printk(KERN_ERR"SSD: Flash %d write BBT ERR!\n",i*ii);
					goto out;
				case  SSD_ECC_ERR:
					printk(KERN_ERR"SSD: Flash %d ECC ERR\n",i*ii);
					goto out;
				case  CHIP_NOT_EXIT:
					printk(KERN_ERR"SSD: Flash %d CHIP NOT EXIT\n",i*ii);
					goto out;
				default:
					printk(KERN_ERR"SSD: Flash %d UNKOWN ERR",i*ii);	
					goto out;
			}
		}
	}

out:
	return ret; 
}
#endif

static int __init ssd_check_init_state(struct ssd_device *sdev)
{
	int ret = 0;
	uint64_t init_state;
	int i;
	int flash_state;
	uint32_t test_data = 0x55555555;
	uint32_t read_data;
	int re_init = 2; // retry twice


	ssd_reg32_write(sdev->ctrlp + FPGA_FLIP_REG, test_data);
	read_data = ssd_reg32_read(sdev->ctrlp + FPGA_FLIP_REG);
	if(read_data != ~(test_data)) {
		//printk(KERN_WARNING "SSD: fpga init error.\n");
		return -1;
	}

	if(mode == SSD_DRV_MODE_DEBUG)
		re_init = 0;

retry:
	init_state = ssd_reg_read(sdev->ctrlp + FPGA_INIT_STATE_REG);

	for(i=0; i<MAX_FLASH_PER_FPGA; i++) {
		flash_state = (init_state >> i) & SSD_FLASH_INIT_STATE_MASK;
		if(!flash_state) {
			if(re_init-- > 0) {
				printk(KERN_WARNING "%s: flash %d init failed, try to recover...\n", sdev->name, i);
				ret = ssd_reset_controller(sdev, 1);
				if(!ret) {
					printk(KERN_WARNING "%s: re-init OK, check init state again\n", sdev->name);
					goto retry;
				} else {
					printk(KERN_ERR "%s: re-init failed.\n", sdev->name);
					break;
				}
			} else {
				printk(KERN_ERR "%s: flash %d init failed\n", sdev->name, i);
				ret = -1;
			}
		}
	}

	return ret; 
}


//Block Device
static void ssd_block_request(struct request_queue *q)
{
    struct ssd_device *dev = q->queuedata;
    struct request *req;
	struct ssd_cmnd *cmd;
    int tag,nsegs,direction,i;
    sector_t block, nr_secs;
    uint32_t * table;
    struct scatterlist *sg;
//	uint32_t * tmp;
//	int j;

	while ((req = blk_peek_request(q)) != NULL) {
#if 0
	   if (!blk_fs_request(req)) 
	   	{
			printk (KERN_NOTICE "SSD: Skip non-fs request\n");
			blkdev_dequeue_request(req);
			req->flags |= REQ_QUIET;
			while (end_that_request_first(req, 0, req->nr_sectors));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
			end_that_request_last(req);
#else
			end_that_request_last(req, 0);
#endif
			continue;
		}
#endif
			
		if (blk_queue_start_tag(q,req)) {
			blk_delay_queue(q, 1);
			break;        //no tag available
		}
		   	
		tag = req->tag;
		table = dev->dmatable_cpu[tag];

		if(likely(!req->special)) {
			cmd = ssd_get_command(dev, GFP_ATOMIC);
			cmd->request = req;
			req->special = cmd;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
			block = req->sector;
			nr_secs = req->nr_sectors;
#else
			block = blk_rq_pos(req);
			nr_secs = blk_rq_sectors(req);
#endif
			if (unlikely(block + nr_secs > get_capacity(req->rq_disk))) {
				printk (KERN_NOTICE "%s: over the max capacity\n", dev->name);
				//blkdev_dequeue_request(req);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
				req->flags |= REQ_QUIET;
				while (end_that_request_first(req, 0, nr_secs));
					blk_queue_end_tag(q, req);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
				end_that_request_last(req);
#else
				end_that_request_last(req, 0);
#endif
#else
				req->cmd_flags |= RQF_QUIET;
				spin_unlock(q->queue_lock);
				blk_end_request(req, -1, nr_secs<<9);
				spin_lock_irq(q->queue_lock);
#endif

				continue;
			}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,20))
			sg_init_table(dev->sg_table[tag], SSD_MAX_SG_SIZE);
#endif
			nsegs = blk_rq_map_sg(q, req, dev->sg_table[tag]);

			if (rq_data_dir(req) == READ) {
				direction = PCI_DMA_FROMDEVICE;
				*table++ = (FUN_READ << 24) |(nsegs << 16) |tag; 
			} else {
				direction = PCI_DMA_TODEVICE;
				*table++ = (FUN_WRITE << 24) |(nsegs << 16) |tag; 
			}
			pci_map_sg(dev->pdev, dev->sg_table[tag], nsegs, direction);

			sg = dev->sg_table[tag];
			for (i = 0; i < nsegs; i++) {
				dma_addr_t  cur_addr;
				uint32_t  cur_len;
				//if(rq_data_dir(req) == READ) { memset(page_address(sg->page) + sg->offset, 0x45, sg->length); }
				cur_addr = sg_dma_address(sg);
				cur_len = sg_dma_len(sg);
				*table++ = (uint32_t) block;
				*table++ = (block >> 32) | ((cur_len >> 9) << 16);
				*table++ = (uint32_t) cur_addr;
				*table++ = (uint32_t)((uint64_t)cur_addr >> 32);
				block += (cur_len >> 9);
				sg++;
			}
			mod_timer(&dev->flush_timer, jiffies + (500 * HZ) / 1000);
		} else {
			cmd = req->special;
			memcpy(table, cmd->cmnd, MAX_COMMAND_SIZE);
		}

		if (unlikely(ssd_dispatch_cmd(cmd))) {          //ssd is busy
			printk("%s: ssd is busy\n", dev->name);
			blk_requeue_request(q, req);
			blk_delay_queue(q, 1);
			break;
		}
		
		//dev->device_busy++;
	}
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static int ssd_block_open(struct inode *inode, struct file *filp)
{
	struct ssd_device *dev = inode->i_bdev->bd_disk->private_data;

#else
static int ssd_block_open(struct block_device *bdev, fmode_t mode)
{
	struct ssd_device *dev = bdev->bd_disk->private_data;
#endif

	if (!try_module_get(dev->owner))
		return -ENODEV;
	
	return 0;
}

static void ssd_block_release(struct gendisk *disk, fmode_t mode)
{
	struct ssd_device *dev = disk->private_data;


	if (ssd_flush(dev))
		return;
	
	module_put(dev->owner);
}

static void reg32_write(uint32_t val, void *addr)
{
	uint32_t iii;
	int i = 0;
	
	writel(val,addr);
	while(1)
	{
		i++;
		iii = readl(addr);
		if (iii & (1 << NFR_STA_SHIFT))    
			break;
		if(i > 10000)
			break;
		cond_resched();
	}
}

static int erase_sector(struct ssd_device *dev, int sect)
{
	uint32_t addr,val = 0,status;
	int i;
	
	addr = sect * 0x20000;
	val = (addr << NFR_ADDR_SHIFT) |(0x20 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
	
	val = (addr << NFR_ADDR_SHIFT) |(0xd0 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
	
	val = (0 << NFR_ADDR_SHIFT) |(0x70 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
	
	for(i=0;i<300;i++) {
		val = (0 << NFR_ADDR_SHIFT) |(0 << NFR_RW_SHIFT);   //read
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		status = readl(dev->ctrlp+NOR_FLASH_REG);
		status = (status >> NFR_DATA_SHIFT) & 0xff;
		if((status & 0x80) == 0x80)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(50);
	}

	if((status & 0x80) != 0x80)
		return -EIO;
	
	val = (0 << NFR_ADDR_SHIFT) |(0x50 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);      //clear status
	
	val = (0 << NFR_ADDR_SHIFT) |(0xff << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);   
	
	if ((status & 0x38) != 0) 
		return -EIO;
	else
		return 0;
}

static int write_buff(struct ssd_device *dev, struct rw_info *wi)
{
	uint8_t *buffer;
	uint32_t i, count=0, remain,start,write_count, val = 0,status;
	int ret=0;
	
	buffer = wi->user_buf;
	
	remain = 0x20000 - (wi->addr & 0x1ffff);
	start = wi->addr;
	
	while(count < wi->len) {
		val = (start << NFR_ADDR_SHIFT) |(0xe8 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		
		for(i=0;i<300;i++) {
			val = (0 << NFR_ADDR_SHIFT) |(0 << NFR_RW_SHIFT);   //read
			reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
			status = readl(dev->ctrlp+NOR_FLASH_REG);
			status = (status >> NFR_DATA_SHIFT) & 0xff;
			if((status & 0x80) == 0x80)
				break;
		}
		if((status & 0x80) != 0x80) {
			ret = -EIO;
			break;
		}
		
		if (wi->len -count <=32 )
			write_count = wi->len - count - 1;
		else if (remain <= 32) {
			write_count = remain -1;
			remain = 0x20000;
		} else {
			write_count = 31;
			remain -= 32;
		}
		
		val = (start << NFR_ADDR_SHIFT) |(write_count << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		for (i=0; i <= write_count; i++) {
			val = (start << NFR_ADDR_SHIFT) |(*(buffer+count) << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
			reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
			start++;
			count++;
		}
		
		val = ((start-1) << NFR_ADDR_SHIFT) |(0xd0 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		
		val = (0 << NFR_ADDR_SHIFT) |(0x70 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		
		for(i=0;i<300;i++) {
			val = (0 << NFR_ADDR_SHIFT) |(0 << NFR_RW_SHIFT);   //read
			reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
			status = readl(dev->ctrlp+NOR_FLASH_REG);
			status = (status >> NFR_DATA_SHIFT) & 0xff;
			if((status & 0x80) == 0x80)
				break;
		}
		
		if ((status & 0x38) != 0) {
			ret = -EIO;
			break;
		}
	}
	val = (0 << NFR_ADDR_SHIFT) |(0x50 << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);      //clear status
	
	val = (0 << NFR_ADDR_SHIFT) |(0xff << NFR_DATA_SHIFT) |(1 << NFR_RW_SHIFT);   //write
	reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
	
	return ret;
}

static int read_buff(struct ssd_device *dev, struct rw_info *ri)
{
	uint8_t *buffer;
	uint32_t  i, val = 0;
	int ret=0;
	
	buffer = ri->user_buf;
	
	for (i=0; i<ri->len; i++) {
		val = ((ri->addr+i) << NFR_ADDR_SHIFT) |(0 << NFR_RW_SHIFT);   //read
		reg32_write(val,dev->ctrlp+NOR_FLASH_REG);
		*(buffer+i) = (readl(dev->ctrlp+NOR_FLASH_REG) >> NFR_DATA_SHIFT) & 0xff;
	}
	
	return ret;
}
/*
static int check_nor(struct ssd_device *dev)
{
	int exist = 0;
	exist = ssd_reg32_read(dev->ctrlp+CPLD_VER_REG) & 0x4;
	if(!exist)
		return 0;
	else
		return 1;
}

static int read_cpld_ver(struct ssd_device *dev)
{
	int ver_lo;
	ver_lo = ssd_reg32_read(dev->ctrlp+CPLD_VER_REG) & 0x3;
	return ver_lo;
}

static int read_pcb_ver(struct ssd_device *dev, uint8_t *board, uint8_t *button)
{
	int ver_lo;
	ver_lo = ssd_reg32_read(dev->ctrlp+PCB_VER_REG);
	*board = (ver_lo & 0x38) >> 3;
	*button = ver_lo & 0x7;
	return ver_lo;
}
*/

static int ssd_check_ioctl_param(struct ssd_device *dev, int flash, int chip, int page, int pagenum)
{
	int status;
//	int chip_page_count;
	int flash_page_count;
	
	if(!pagenum)
		return -EINVAL;

	if(flash > dev->fpga_info.flash_count)
		return -EINVAL;
/*
	if(chip > MAX_CHIPS_PER_FLASH || flash > dev->fpga_info.flash_count)
		return -EINVAL;
*/
	status = (dev->fpga_info.flash_status >> (flash*SSD_FLASH_STATUS_BITS)) & SSD_FLASH_STATUS_MASK;
	if(!(status & SSD_FLASH_EXIST_MASK)) 
		return -EINVAL;
/*
	if(!(status & SSD_FLASH_CHIPS_MASK) && chip == 1)
		return -EINVAL;

	chip_page_count = ((uint32_t)dev->fpga_info.block_count * (uint32_t)dev->fpga_info.page_count)/(chip + 1);
	if((page + pagenum) > chip_page_count)
		return -EINVAL;
*/
	flash_page_count = ((uint32_t)dev->fpga_info.block_count * (uint32_t)dev->fpga_info.page_count);
	if((page + pagenum) > flash_page_count)
		return -EINVAL;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16))
static int ssd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ssd_device *dev = bdev->bd_disk->private_data;

	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = (dev->size & ~0x3f) >> 6;
	return 0;
}
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static int ssd_block_ioctl(struct inode *inode, struct file *file, 
			      unsigned int cmd, unsigned long arg)
{
	struct ssd_device *dev = inode->i_bdev->bd_disk->private_data;
#else
static int ssd_block_ioctl(struct block_device *bdev, fmode_t mode,
			      unsigned int cmd, unsigned long arg)
{
	struct ssd_device *dev = bdev->bd_disk->private_data;
#endif
	int i;

	void __user *argp = (void __user *)arg;
	void __user *buf; 
	int64_t ret = 0;
	uint8_t *kbuf = NULL;
//	uint64_t size;
/*	
	if(cmd == CMD_NOR_ERASE ||
		cmd == CMD_NOR_READ ||
		cmd == CMD_NOR_WRITE) {
		for(i = 0;i < MAX_FPGA_NUM ; i++) {
			if(ssd_table[i] && i == nor_fpga[0]) {
				dev = ssd_table[i];
				break;
			}            
		}
	}
*/
	switch (cmd) {
		case HDIO_GETGEO:
		{
			struct hd_geometry geo;
			geo.cylinders = (dev->size & ~0x3f) >> 6;
			geo.heads = 4;
			geo.sectors = 16;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
			geo.start = get_start_sect(inode->i_bdev);
#else
			geo.start = get_start_sect(bdev);
#endif
			if (copy_to_user(argp, &geo, sizeof(geo)))
				return -EFAULT;
		}
		break;

		case CMD_NOR_ERASE:
		{
			struct erase_info ei;

			if (copy_from_user(&ei, argp, sizeof(struct erase_info)))
				return -EFAULT;

			for (i = 0; i < ei.count; i++) {
 				if (erase_sector(dev, ei.sect + i)) {
					printk("%s: erase failure\n", dev->name);
					return -EFAULT;
				}
			}
		}
		break;

		case CMD_NOR_WRITE:
		{
			struct rw_info wi, kwi;
			uint8_t *buffer = NULL;

			if (copy_from_user(&wi, argp, sizeof(struct rw_info)))
				return -EFAULT;

			buffer = vmalloc(wi.len);
			if (buffer == NULL)
				return -ENOMEM;

			if (copy_from_user(buffer, wi.user_buf,wi.len)) {
				vfree(buffer);
				return -EFAULT;
			}

			kwi.addr = wi.addr;
			kwi.len  = wi.len;
			kwi.user_buf = buffer;

			if (write_buff(dev,&kwi)) {
				printk("%s: write failure\n", dev->name);
				return -EIO;
			}
			vfree(buffer);
		}
		break;

		case CMD_NOR_READ:
		{
			struct rw_info ri, kri;
			uint8_t *buffer = NULL;

			if (copy_from_user(&ri, argp, sizeof(struct rw_info)))
				return -EFAULT;

			buffer = vmalloc(ri.len);
			if (buffer == NULL)
				return -ENOMEM;
			kri.addr = ri.addr;
			kri.len  = ri.len;
			kri.user_buf = buffer;

			if (read_buff(dev,&kri)) {
				printk("%s: read failure\n", dev->name);
				vfree(buffer);
				return -EIO;
			}

			if (copy_to_user(ri.user_buf, buffer, ri.len)) {
				vfree(buffer);
				return EFAULT;
			}
			vfree(buffer);
			
		}
		break;

		case BLKFLSBUF:
		if (ssd_flush(dev)) {
			printk("%s: flush failure\n", dev->name);
			return -EFAULT;
		}
		break;

		case CMD_NAND_READ:
		{
			struct ssd_flash_op_info_user user_nand_op;
			int flash, chip, page, pagenum;
			uint32_t total_page = 0;
			uint32_t length;
			
			if(copy_from_user(&user_nand_op, argp, sizeof(struct ssd_flash_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error  ");
				return -EFAULT;
			}
			flash = user_nand_op.flash;
			chip = user_nand_op.chip;
			page = user_nand_op.page;
			pagenum = user_nand_op.pagenum;
			buf = user_nand_op.buf;

			length = dev->fpga_info.page_size;

 			ret= ssd_check_ioctl_param(dev, flash, chip, page, pagenum);
			if(ret)
				return ret;

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				printk(KERN_WARNING "Unable to allocate read read buffer.\n");
				return -ENOMEM;
			}

			while(pagenum) {

				ret = ssd_chip_read(dev, kbuf, flash, chip, page, 1);
				if(unlikely(ret < 0 && ret != -EBADMSG)) {
					goto err_out_kbuf;
				}

				if((ret = copy_to_user(buf, kbuf, length)) != 0) {
					goto err_out_kbuf;
				}

				buf+=length;
				page += 1;
				pagenum -= 1;
				total_page += 1;
			}

			kfree(kbuf);
			kbuf = NULL;
			//if(copy_to_user(&user_nand_op.done, &total_page, sizeof(uint32_t)))
			//	return -EFAULT;
			
			ret = 0;
		}
		break;
		case CMD_NAND_READ_WITH_OOB:
		{
			struct ssd_flash_op_info_user user_nand_op;
			int flash, chip, page, pagenum;
			uint32_t total_page = 0;
			uint32_t length;
			
			if(copy_from_user(&user_nand_op, argp, sizeof(struct ssd_flash_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}
			flash = user_nand_op.flash;
			chip = user_nand_op.chip;
			page = user_nand_op.page;
			pagenum = user_nand_op.pagenum;
			buf = user_nand_op.buf;

			length = dev->fpga_info.page_size + dev->fpga_info.oobsize;

 			ret= ssd_check_ioctl_param(dev, flash, chip, page, pagenum);
			if(ret)
				return ret;

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				printk(KERN_WARNING "Unable to allocate read read buffer.\n");
				return -ENOMEM;
			}

			while(pagenum) {

				ret = ssd_chip_read_with_oob(dev, kbuf, flash, chip, page, 1);
				if(unlikely(ret < 0 && ret != -EBADMSG)) {
					goto err_out_kbuf;
				}

				if((ret = copy_to_user(buf, kbuf, length)) != 0) {
					goto err_out_kbuf;
				}

				buf+=length;
				page += 1;
				pagenum -= 1;
				total_page += 1;
			}

			kfree(kbuf);
			kbuf = NULL;
			//if(copy_to_user(&user_nand_op.done, &total_page, sizeof(uint32_t)))
			//	return -EFAULT;
			ret = 0;
		}
		break;

		case CMD_NAND_ERASE:
		{
			struct ssd_flash_op_info_user user_nand_op;
			int flash, chip, page, pagenum;
			
			if(copy_from_user(&user_nand_op, argp, sizeof(struct ssd_flash_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}

			flash = user_nand_op.flash;
			chip = user_nand_op.chip;
			page = user_nand_op.page;
			pagenum = user_nand_op.pagenum;
			

 			ret= ssd_check_ioctl_param(dev, flash, chip, page, pagenum);
			if(ret)
				return ret;

			if((page % dev->fpga_info.page_count != 0) ||
				(pagenum % dev->fpga_info.page_count != 0))
				return -EINVAL;


			while(pagenum) {
				//printk(KERN_WARNING "erase fs = %llx\n", ofs);
				ret = ssd_chip_erase(dev, flash, chip, page);
				if(unlikely(ret < 0))
					return ret;

				page += dev->fpga_info.page_count;
				pagenum -= dev->fpga_info.page_count;
			}
			ret = 0;
			return ret;
		}
		break;

		case CMD_NAND_GET_CHIPID:
		{
			struct ssd_id_info_user  id_info;
			uint32_t chip_no,length;
			char *id;
			//		int i;

			if(copy_from_user(&id_info, argp, sizeof(struct ssd_id_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}
			
			chip_no = id_info.chip;
			length = id_info.length;
			buf = id_info.buf;

			if(unlikely(length > SSD_MAX_CHIP_ID_SIZE))
				return -EINVAL;

			id = kmalloc(SSD_MAX_CHIP_ID_SIZE, GFP_KERNEL);
			if(!id)
				return -ENOMEM;
			memset(id, 0, SSD_MAX_CHIP_ID_SIZE);
			ret = ssd_get_chip_id(dev, chip_no,id);
 			if(unlikely(ret < 0)) {
				kfree(id);
				return ret;
			}

			/*
			for(i=0;i<SSD_MAX_CHIP_ID_SIZE;i++) {
			printk("%x ", id[i]);
			}
			printk("\n");*/
			if(copy_to_user(buf,id, SSD_MAX_CHIP_ID_SIZE)) {
				kfree(id);
				return -EFAULT;
			}

			kfree(id);
		}
		break;

		case CMD_RAM_READ:
		{
			//printk(KERN_WARNING "ret = %lld\n", ret);
			struct ssd_op_info_user  user_op ;
			loff_t ofs;
			uint32_t length;

			if(copy_from_user(&user_op, argp, sizeof(struct ssd_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}
			ofs = user_op.start;
			length = user_op.length;
			buf = user_op.buf;
			ret = -length;

			if(ofs >= dev->fpga_info.ramsize || ofs + length > dev->fpga_info.ramsize || !length)
				return -EINVAL;

			if(unlikely(length > RAM_OP_SIZE)) {
				printk(KERN_WARNING "Too large length %d!\n",length);
				ret = -EINVAL;
				return ret;
			}

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				printk(KERN_WARNING "Unable to allocate read DMA buffer.\n");
				ret = -ENOMEM;
				goto err_out_kbuf;
			}
			//memset(kbuf, 0xaa, size);
			ret = ssd_ram_read(dev, kbuf,length, ofs);
			//ret = length;
			if(unlikely(ret < 0)) {
				ret = -length;
				goto err_out_kbuf;
			}

			if((ret = copy_to_user(buf,kbuf,length)) != 0) {
				goto err_out_kbuf;
			}

			
			//if(copy_to_user(&user_op.done, &length, sizeof(uint32_t)))
			//	return -EFAULT;
			kfree(kbuf);
			//kbuf = NULL;
			//printk(KERN_WARNING "ret = %lld\n", ret);
			return 0;
		}
		break;

		case CMD_RAM_WRITE:
		{
		//printk(KERN_WARNING "ret = %lld\n", ret);
			struct ssd_op_info_user  user_op ;
			loff_t ofs;
			uint32_t length;

			if(copy_from_user(&user_op, argp, sizeof(struct ssd_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}
			ofs = user_op.start;
			length = user_op.length;
			buf = user_op.buf;
			ret = -length;

			if(ofs >= dev->fpga_info.ramsize || ofs + length > dev->fpga_info.ramsize)
				return ret;
			if(!length)
				return 0;

			if(unlikely(length > RAM_OP_SIZE)) {
				printk(KERN_WARNING "Too large length %d!\n",length);
				return ret;
			}

			kbuf = kmalloc(length, GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				goto err_out_kbuf;
			}

			if((ret = copy_from_user(kbuf, buf, length)) != 0) {
				ret = -length;
				goto err_out_kbuf;
			}
			ret = ssd_ram_write(dev, kbuf, length, ofs);
			//ret = length;
			if(unlikely(ret < 0)) {
				ret = -length;
				goto err_out_kbuf;
			}

			ret = 0;
			//if(copy_to_user(&user_op.done, &length, sizeof(uint32_t)))
			//	return -EFAULT;
			kfree(kbuf);
			kbuf = NULL;
			//printk(KERN_WARNING "ret = %lld\n", ret);
		}
		break;
		
		case CMD_RAM_9STEP:
		{
			ret = ssd_ram_9step(dev);
			return ret;
		}
		break;
		
		case CMD_RAM_3STEP:
		{
			ret = ssd_ram_3step(dev);
			return ret;
		}
		break;
#if 0
		case CMD_SSD_BLOCK_IS_BAD:
		{
			if (copy_from_user(&ofs, argp, sizeof(loff_t)))
			return -EFAULT;

			//printk("CMD_SSD_BLOCK_IS_BAD ofs = 0x%llx\n", ofs);
			return block_isbad(dev, ofs);
		}
		break;
#endif

		case CMD_SSD_BLOCK_IS_BAD_DIRECT:
		{
			struct ssd_flash_op_info_user user_nand_op;
			int flash, chip, page;
			
			if(copy_from_user(&user_nand_op, argp, sizeof(struct ssd_flash_op_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}

			flash = user_nand_op.flash;
			chip = user_nand_op.chip;
			page = user_nand_op.page;

			ret = ssd_check_ioctl_param(dev, flash, chip, page, dev->fpga_info.page_count);
			if(ret)
				return -EINVAL;

			if((page % dev->fpga_info.page_count) != 0)
				return -EINVAL;

			//printk("CMD_SSD_BLOCK_IS_BAD ofs = 0x%llx\n", ofs);
			return block_isbad_direct(dev, flash, chip, page);
		}
		break;

		case CMD_SSD_GET_BBT:
		{
			struct ssd_bbt_info_user bbt_info;
			int flash, length, mode;

			if(copy_from_user(&bbt_info, argp, sizeof(struct ssd_bbt_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}
			
			flash = bbt_info.flash;
			length = bbt_info.length;
			buf = bbt_info.buf;
			mode = bbt_info.mode;

			kbuf = kmalloc(length ,GFP_KERNEL);
			if (!kbuf) {
				ret = -ENOMEM;
				goto err_out_kbuf;
			}
			
			ret = ssd_read_bbt(dev, flash, kbuf, length, mode);
			if(unlikely(ret < 0))
				goto err_out_kbuf;

			if((ret = copy_to_user(buf,kbuf,length)) != 0) {
				goto err_out_kbuf;
			}

			kfree(kbuf);
			kbuf = NULL;
			return 0;
		}
		break;

		case CMD_SSD_ERASE_BBT:
		{
			int flash;

			if(copy_from_user(&flash, argp, sizeof(int))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}

			ret = ssd_erase_bbt(dev, flash);

			return ret;
		}
		break;

		case CMD_SSD_MARK_BAD:
		{
			struct ssd_mark_info_user mark_info;
			int flash, block;
			
			if(copy_from_user(&mark_info, argp, sizeof(struct ssd_mark_info_user))) {
				printk(KERN_WARNING "copy_from_user error\n");
				return -EFAULT;
			}

			flash = mark_info.flash;
			block = mark_info.block;
			if(flash > dev->fpga_info.flash_count || 
				block > dev->fpga_info.block_count)
				return -EINVAL;

			ret = ssd_mark_bad(dev, flash, block);

			return ret;
		}
		break;

		case CMD_SSD_UPDATE_BBT:
		{
			ret = ssd_flush_allbbt(dev);

			return ret;
		}
		break;

		case CMD_SSD_GET_BITFLIP:
		{
			
#ifndef SHOW_BITFLIP
			return -EINVAL;
#else
			struct ssd_bitflip_info_user *bitflip_info = (struct ssd_bitflip_info_user __user *)arg;

			if((ret = copy_to_user(bitflip_info->bitflip, dev->bitflip, 16*sizeof(uint64_t))) != 0) {
				return -EFAULT;
			}

			bitflip_info->read_amount = dev->read_amount;
			return 0;
#endif
		}
		break;

		case CMD_FPGA_RESET:
		{
			int flag;
			
			ret = copy_from_user(&flag, argp, sizeof(int));
			if(ret) {
				printk(KERN_WARNING "copy_from_user error\n");
				return ret;
			}

			if((flag != 0) && (flag != 1))
				return -EINVAL;
			
			return ssd_reset_controller(dev, flag);
		}
		break;

		case CMD_SSD_BURN_FLAG:
		{
			int flag;
			

			ret = copy_from_user(&flag, argp, sizeof(int));
			if(ret) {
				printk(KERN_WARNING "copy_from_user error\n");
				return ret;
			}

			if((flag != 0) && (flag != 1))
				return -EINVAL;

			ssd_reg32_write(dev->ctrlp + FPGA_BURN_FLAG_REG, flag);
			
			return 0;
		}
		break;

		case CMD_FPGA_GET_INFO:
		{
			struct ssd_info_user ssd_info;
			
			ssd_info.size		= dev->fpga_info.phy_size;
			ssd_info.flash_status		= dev->fpga_info.flash_status;
			ssd_info.phys_flash_status		= dev->fpga_info.phys_flash_status;
			ssd_info.phys_init_state        = dev->fpga_info.phys_init_state;
			ssd_info.flashsize	= dev->fpga_info.flash_size;
			ssd_info.blocksize	= dev->fpga_info.block_size;
			ssd_info.flashcount = dev->fpga_info.flash_count;
			ssd_info.phys_flashcount = dev->fpga_info.phys_flash_count;
			ssd_info.blockcount	= dev->fpga_info.block_count;
			ssd_info.pagesize	= dev->fpga_info.page_size;
			ssd_info.oobsize	= dev->fpga_info.oobsize;
			ssd_info.pagecount	= dev->fpga_info.page_count;
			ssd_info.version    = dev->fpga_ver | (((struct ssd_card *)dev->parent)->cpld_ver<<16);

			//printk(KERN_WARNING "chipsize =%llx, cc = %d\n",dev->fpga_info.chip_size, info_tmp.chipcount);
			if(copy_to_user(argp, &ssd_info, sizeof(struct ssd_info_user)))
				return -EFAULT;
		}
		break;
		
		case CMD_SSD_RELOAD_FPGA:
		{
			ret = copy_from_user(&reload_fpga, argp, sizeof(int));
			if(ret) {
				printk(KERN_WARNING "copy_from_user error\n");
				return ret;
			}
			if((reload_fpga != 0) && (reload_fpga != 1))
				return -EINVAL;
		}
		break;
		
		case CMD_GET_LOG:
		{
			struct ssd_log_info_user *arg_info = NULL;
			struct ssd_log_info_user log_info;
			memset(&log_info, 0, sizeof(struct ssd_log_info_user));
			log_info.buf = vmalloc(sizeof(uint64_t) * SSD_MAX_LOG);
			if(!log_info.buf) {
				return -EFAULT;
			}
			memset(log_info.buf, 0, sizeof(uint64_t) * SSD_MAX_LOG);
			if(ssd_put_log(dev, &log_info)) {
				goto out_get_log;
			}
			arg_info = (struct ssd_log_info_user *)argp;
			if(copy_to_user(arg_info->buf, log_info.buf, sizeof(uint64_t)*SSD_MAX_LOG)){
                        	goto out_get_log;
			}
			if(copy_to_user(&arg_info->time, &log_info.time, sizeof(struct timeval)))
				goto out_get_log;
			if(copy_to_user(&arg_info->count, &log_info.count, sizeof(uint32_t)))
				goto out_get_log;
			if(copy_to_user(&arg_info->idx, &log_info.idx, sizeof(uint32_t)))
				goto out_get_log;

			vfree(log_info.buf);
			break;
out_get_log:
			vfree(log_info.buf);
			return -EFAULT;
		}
		break;

		case CMD_GET_CARD_NUM:
		{
			struct cardinfo *cinfo = (struct cardinfo *)argp;
			cinfo->count = ssd_card_count;
			for(i = 0; i < ssd_card_count; i++) {
				cinfo->card[i] =ssd_card_table[i]->primary;
			}
		}
		break;

		case CMD_GET_SSD_STATUS:
		{
			printk(KERN_WARNING "E8 = %llx\n", ssd_reg_read(dev->ctrlp + FPGA_SSD_STATUS_REG));
			printk(KERN_WARNING "F0 = %llx\n", ssd_reg_read(dev->ctrlp + FPGA_SSD_STATUS_REG1));
			printk(KERN_WARNING "F8 = %llx\n", ssd_reg_read(dev->ctrlp + FPGA_SSD_STATUS_REG2));
		}
		break;

		case CMD_GET_PCB_VER:
		{
			struct ssd_pcbinfo pcb_info, pcb_info_tmp;
			int index;
			ret = copy_from_user(&pcb_info, argp, sizeof(struct ssd_pcbinfo));
			if(ret) {
				printk(KERN_WARNING "copy_from_user error\n");
				return ret;
			}

			index = pcb_info.index;
			if(index > ssd_card_count|| index == 0)
				return -EINVAL;

			index -= 1;

			pcb_info_tmp.pcb_ver = ssd_card_table[index]->pcb_ver;
			pcb_info_tmp.upper_pcb_ver = ssd_card_table[index]->upper_pcb_ver;

			if((ret = copy_to_user(argp, 
				(&pcb_info_tmp), sizeof(struct ssd_pcbinfo))) != 0) {
				printk(KERN_WARNING "copy_to_user error\n");
				return ret;
			}
		}
		break;
		
		case CMD_SSDLOGD_PID:
		{
			ret = copy_from_user(&log_pid, argp, sizeof(pid_t));
			if(ret) {
				printk(KERN_WARNING "copy_from_user error\n");
				return ret;
			}
			//printk("log pid is %d\n", log_pid);
		}
		break;

		default:
			return -EINVAL;
	}


	err_out_kbuf:
	if(NULL != kbuf) kfree(kbuf);
	return  ret;
}

struct block_device_operations SSD_ops = {
	.owner		= THIS_MODULE,
	.open		= ssd_block_open,
	.release	= ssd_block_release,
	.ioctl		= ssd_block_ioctl,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16))
	.getgeo		= ssd_getgeo,
#endif

};

#if 0
static int end_req_thread(void *arg)
{
   struct ssd_device *dev = arg;
   struct request_queue *rq;
   uint64_t flag;
   int tag, i;
   uint32_t tag_flag,status;
   struct request *req;

   rq = dev->queue;
   daemonize("%s", dev->gd->disk_name);

   while (!dev->exiting) 
   	{
   	   spin_lock_irq(&dev->map_lock);
   	   flag = dev->rq_flg;
          spin_unlock_irq(&dev->map_lock);
          tag_flag = (uint32_t)flag;
          status = (uint32_t)(flag >> 32);
	   
          while (tag_flag)
          	{
          	  spin_lock_irq(rq->queue_lock);
	   	  tag = find_first_bit(&tag_flag,32);
	         req = blk_queue_find_tag(rq,tag);
	         if (req)
	            {
	               if ((status >> tag) & 1)
	               	i = 0;	//failure
	               else 
	               	i = 1;	//success
		       	while (end_that_request_first(req, i, req->nr_sectors));
		       	blk_queue_end_tag(rq, req);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
			end_that_request_last(req);
#else
			end_that_request_last(req, 0);
#endif
	             }
	         
	         clear_bit(tag,&tag_flag);
	         //spin_lock_irq(&dev->map_lock);
   	   	  clear_bit(tag,&dev->rq_flg);
   	   	  clear_bit(tag+32,&dev->rq_flg);
        	 // spin_unlock_irq(&dev->map_lock);
	         spin_unlock_irq(rq->queue_lock);
          	}

	   wait_event(dev->wait_q,(dev->rq_flg || dev->exiting));
   	}
   complete_and_exit(&dev->thread_dead, 0);
}
#endif

static void ssd_end_request(unsigned long data)
{
	struct ssd_device *dev;
	struct request_queue *rq;
	LIST_HEAD(local_q);
	struct ssd_cmnd* cmd;
	struct request *req;
	sector_t nr_secs;
	unsigned long flags;

	local_irq_disable();
	list_splice_init(&get_cpu_var(ssd_done_q), &local_q);
	local_irq_enable();

	while (!list_empty(&local_q)) {
		cmd = list_entry(local_q.next, struct ssd_cmnd, list);
		list_del_init(&cmd->list);
		dev = (struct ssd_device *)cmd->device;
		rq = dev->queue;
		req = cmd->request;

		if (likely(req)) {
			if(likely(cmd->cmd_type == SSD_CMD_BLOCK)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
				nr_secs = req->nr_sectors;
#else
				nr_secs = blk_rq_sectors(req);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
				while (end_that_request_first(req, !(cmd->errors), nr_secs));
				spin_lock_irqsave(rq->queue_lock, flags);
				blk_queue_end_tag(rq, req);
				//dev->device_busy--;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
				end_that_request_last(req);
#else
				end_that_request_last(req, !(cmd->errors));
#endif
				spin_unlock_irqrestore(rq->queue_lock, flags);
#else
				if(blk_end_request(req, (int)(cmd->errors)*(-1),nr_secs<<9)) {
					printk(KERN_WARNING "%s: request left over\n", dev->name);
				}
#endif		
				ssd_put_command(cmd);
			} else if(cmd->cmd_type == SSD_CMD_OTHER) {
				if (req->cmd_flags & RQF_QUEUED) {
					spin_lock_irqsave(rq->queue_lock, flags);
					blk_queue_end_tag(rq, req);
					//dev->device_busy--;
					spin_unlock_irqrestore(rq->queue_lock, flags);
					if(cmd->waiting) {
						complete(cmd->waiting);
					}
				}
			} else {
				printk(KERN_WARNING "ssd_end_request error\n");
			}
			blk_run_queue(rq);
		}
	}
}


static void del_SSD(struct ssd_device * dev)
{
	struct ssd_cmnd *cmd;
	int i;

	if(dev == NULL)
		return;
	
	ssd_flush(dev);
	del_timer_sync(&dev->flush_timer);
	if (dev->gd)  {
		del_gendisk(dev->gd);
		put_disk(dev->gd);
	}
	if (dev->queue) {
		blk_cleanup_queue(dev->queue);
	}
	//dev->exiting = 0;
	//dev->rq_flg = 0;

	for (i=0;i<SSD_CMD_FIFO_DEPTH;i++) {
		if (dev->sg_table[i])
		kfree(dev->sg_table[i]);
	}

	while (!list_empty(&dev->free_list)) {
		cmd = list_entry(dev->free_list.next, struct ssd_cmnd, list);
		list_del_init(&cmd->list);
		kmem_cache_free(dev->cmd_slab, cmd);
	}

	kmem_cache_destroy(dev->cmd_slab);
}

static int add_SSD(struct ssd_device * dev)
{
	struct ssd_cmnd *cmd;
	int i;

	if(dev == NULL)
		goto out;

	spin_lock_init(&dev->free_list_lock);
	INIT_LIST_HEAD(&dev->free_list);

	dev->cmd_slab = kmem_cache_create(dev->name,
				sizeof(struct ssd_cmnd), 0,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
				SLAB_HWCACHE_ALIGN, NULL, NULL);
#else
				SLAB_HWCACHE_ALIGN, NULL);
#endif
	if (!dev->cmd_slab)
		goto err_out_slab;

	for(i=0;i<SSD_CMD_FIFO_DEPTH*2;i++) {
		cmd = kmem_cache_alloc(dev->cmd_slab,
					GFP_KERNEL);
		if (!cmd)
			goto err_out_slab_alloc;
		
		memset(cmd, 0, sizeof(*cmd));
		cmd->device = dev;
		cmd->from = SSD_CMND_FROM_FREE;
		init_timer(&cmd->eh_timeout);
		INIT_LIST_HEAD(&cmd->list);
		
		list_add_tail(&cmd->list, &dev->free_list);	
	}

	memset(dev->sg_table,0,sizeof(dev->sg_table));
	for (i=0;i<SSD_CMD_FIFO_DEPTH;i++) {
		dev->sg_table[i] = kmalloc(sizeof(struct scatterlist) * 128,GFP_KERNEL);

		if (dev->sg_table[i] ==NULL) {
			printk("%s: no enough memery for sg_table\n", dev->name);
			goto err_out_nomem;
		}
	}

	spin_lock_init(&dev->queue_lock);
//	init_completion(&dev->thread_dead);
	//dev->exiting = 0;
	//dev->rq_flg = 0;

	dev->queue = blk_init_queue(ssd_block_request, &dev->queue_lock);
	if (dev->queue == NULL) {
		printk("%s: blk_init_queue failure\n ", dev->name);
		goto err_out_queue;
	}
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9))
	dev->queue->nr_requests = 128;
#endif

	blk_queue_max_segments(dev->queue, SSD_MAX_SG_SIZE);
	blk_queue_max_hw_sectors(dev->queue, 8192);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	blk_queue_hardsect_size(dev->queue, 512);
#else
	blk_queue_logical_block_size(dev->queue, 512);
#endif
	dev->queue->queuedata = dev;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	blk_queue_issue_flush_fn(dev->queue, ssd_issue_flush_fn);
#endif

	init_timer(&dev->flush_timer);
	dev->flush_timer.function = ssd_flush_timer;
	dev->flush_timer.data = (unsigned long)dev;

	if (blk_queue_init_tags(dev->queue, SSD_CMD_FIFO_DEPTH, NULL, 0)) {
		printk("%s: blk_queue_init_tags failure\n", dev->name);
		goto err_out_tags;
	}
	if(likely(mode != SSD_DRV_MODE_DEBUG)) {
		dev->gd = alloc_disk(SSD_MINORS);
	} else {
		dev->gd = alloc_disk(1);
	}
	if (!dev->gd) {
		printk (KERN_NOTICE "%s: alloc_disk failure\n", dev->name);
		goto err_out_gd;
	}
	dev->gd->major = ssd_major;
	if(likely(mode != SSD_DRV_MODE_DEBUG)) {
		dev->gd->first_minor = dev->controller_idx * SSD_MINORS;
	} else {
		dev->gd->first_minor = dev->controller_idx * 1;
	}
	dev->gd->fops = &SSD_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, sizeof(dev->gd->disk_name), "%s", dev->name);
	//if(likely(mode != SSD_DRV_MODE_DEBUG)) {
		set_capacity(dev->gd, dev->size >> 9);
	//} else {
	//	set_capacity(dev->gd, 10); //for debug
	//}
	
	add_disk(dev->gd);
	return 0;
		
err_out_gd:
err_out_tags:	
	blk_cleanup_queue(dev->queue);
err_out_queue:
err_out_nomem:
	for (i=0;i<SSD_CMD_FIFO_DEPTH;i++) {
		if (dev->sg_table[i])
			kfree(dev->sg_table[i]);
	}
err_out_slab_alloc:
	while (!list_empty(&dev->free_list)) {
		cmd = list_entry(dev->free_list.next, struct ssd_cmnd, list);
		list_del_init(&cmd->list);
		kmem_cache_free(dev->cmd_slab, cmd);
	}
	kmem_cache_destroy(dev->cmd_slab);
err_out_slab:

out:
	return -ENOMEM;
}

static struct ssd_cmnd *ssd_get_command(struct ssd_device *dev, int gfp_mask) {
	struct ssd_cmnd *cmd;
	enum ssd_cmnd_from from = SSD_CMND_FROM_SLAB;
	
	cmd = kmem_cache_alloc(dev->cmd_slab, gfp_mask);

	if (unlikely(!cmd)) {
		unsigned long flags;

		spin_lock_irqsave(&dev->free_list_lock, flags);
		if (likely(!list_empty(&dev->free_list))) {
			cmd = list_entry(dev->free_list.next,
					 struct ssd_cmnd, list);
			list_del_init(&cmd->list);
		}
		spin_unlock_irqrestore(&dev->free_list_lock, flags);
		from = SSD_CMND_FROM_FREE;
	}

	if (likely(cmd != NULL)) {
		memset(cmd, 0, sizeof(*cmd));
		cmd->device = dev;
		cmd->from = from;
		init_timer(&cmd->eh_timeout);
		INIT_LIST_HEAD(&cmd->list);
	}

	return cmd;

}

static void ssd_put_command(struct ssd_cmnd *cmd) {
	struct ssd_device *sdev = cmd->device;
	unsigned long flags;

	if(unlikely(cmd->from == SSD_CMND_FROM_FREE)) {
		spin_lock_irqsave(&sdev->free_list_lock, flags);
		list_add_tail(&cmd->list, &sdev->free_list);
		cmd = NULL;
		spin_unlock_irqrestore(&sdev->free_list_lock, flags);
	}

	if (likely(cmd != NULL))
		kmem_cache_free(sdev->cmd_slab, cmd);
}

#define LABEL_START	0x300000
#define STRIPE_SIZE 0x340000

#define LABEL_START_NEW	0x380000
#define STRIPE_SIZE_NEW 0x3C0000

static int ssd_save_config(struct ssd_device *dev, struct ssd_cfg *cfg)
{
	struct rw_info rw;
	
	memset(&rw, 0, sizeof(struct rw_info));
	
	rw.addr		= STRIPE_SIZE;
	if(dev->fpga_ver >= FPGA_VER_NEW) {
		rw.addr = STRIPE_SIZE_NEW;
	}
	
	rw.len		= sizeof(struct ssd_cfg);
	if(SSD_DRV_MODE_RB_COMPATIBILITY == mode) {
		rw.len -= sizeof(cfg->rb_percent);
	}
	rw.user_buf	= (void *)cfg;
	
	if(erase_sector(dev, 26)) {
		return -EIO;
	}
	if(write_buff(dev, &rw)) {
		return -EIO;
	}
	return 0;
}

static int ssd_get_config(struct ssd_device *dev, struct ssd_cfg *cfg)
{
	struct rw_info rw;
	
	memset(&rw, 0, sizeof(struct rw_info));
	
	rw.addr		= STRIPE_SIZE;
	if(dev->fpga_ver >= FPGA_VER_NEW) {
		rw.addr = STRIPE_SIZE_NEW;
	}
	
	rw.len		= sizeof(struct ssd_cfg);
	rw.user_buf = (void *)cfg;

	if(read_buff(dev, &rw)) {
		printk(KERN_WARNING "%s: read config from nor flash error\n", dev->name);
		return -EIO;
	}
	
	return 0;
}

static int show_ssd(int idx)
{
	struct rw_info rw;
	struct ssd_device *dev = ssd_card_table[idx]->primary_dev;
	unsigned char sn[32];

	memset(&rw, 0, sizeof(struct rw_info));
	memset(sn, 0, 32);

	if(dev) {
		rw.addr = LABEL_START;
		if(dev->fpga_ver >= FPGA_VER_NEW) {
			rw.addr = LABEL_START_NEW;
		}

		rw.addr += 0x20; // offset of the sn
		
		rw.len = 32;
		rw.user_buf = (void *)sn;

		if(read_buff(dev, &rw)) {
			return -EIO;
		}
	}

	if(strlen(sn) != 0 && sn[0] != 0xff) {
		printk(KERN_WARNING "SSD %d (SN %s) got some problem.\n", idx+1, sn);
	} else {
		printk(KERN_WARNING "SSD %d got some problem.\n", idx+1);
	}
	return 0;
}

static int ssd_config(int idx)
{
	int ret = 0;
	int rb = 0;
	int data_page_count;
	int rb_compatibility=0;
	struct ssd_device *sdev;
	struct ssd_device *itor;
	struct ssd_cfg user_cfg, now_cfg, default_cfg, new_cfg;

	if(!ssd_card_table[idx]) {
		goto out;
	}

	sdev = ssd_card_table[idx]->primary_dev;
	if(sdev == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if(SSD_DRV_MODE_RB_COMPATIBILITY == mode &&
		ssd_card_table[idx]->version < FPGA_VER_NEW) { // New Ver. do not use RB compatibility mode
		rb_compatibility = 1;
	}

	if(rb_compatibility) {
		if(ssd_card_table[idx]->version & 0x1) {
			rb_percent = SSD_OLD_VER_RESERVED_BLOCKS_NEW_ARITH;
		} else {
			rb_percent = SSD_OLD_VER_RESERVED_BLOCKS;
		}
	}

	user_cfg.stripe_size = (uint32_t)stripe_size;
	user_cfg.rb_percent  = (uint32_t)rb_percent;

	default_cfg.stripe_size = SSD_DEFAULT_STRIPE_SIZE;
	default_cfg.rb_percent  = SSD_SLC_DEFAULT_RESERVED_BLOCKS;
	if(ssd_card_table[idx]->type == SSD_TYPE_MLC)
		default_cfg.rb_percent  = SSD_MLC_DEFAULT_RESERVED_BLOCKS;

	new_cfg.stripe_size = user_cfg.stripe_size;
	new_cfg.rb_percent  = user_cfg.rb_percent;
	
	ret = ssd_get_config(sdev, &now_cfg);
	if(ret)
		goto out;

	if(new_cfg.stripe_size != SSD_STRIPE_4K && new_cfg.stripe_size != SSD_STRIPE_64K) {
		new_cfg.stripe_size = now_cfg.stripe_size;
		if(new_cfg.stripe_size != SSD_STRIPE_4K && new_cfg.stripe_size != SSD_STRIPE_64K) {
			new_cfg.stripe_size = default_cfg.stripe_size;
		}
	}

	if(!rb_compatibility && 
		(new_cfg.rb_percent > SSD_MAX_RESERVED_BLOCKS || 
		new_cfg.rb_percent < default_cfg.rb_percent)) {
		new_cfg.rb_percent = now_cfg.rb_percent;
		if(new_cfg.rb_percent > SSD_MAX_RESERVED_BLOCKS || 
			new_cfg.rb_percent < default_cfg.rb_percent) {
			new_cfg.rb_percent = default_cfg.rb_percent;
		}
	}

	if(0 != memcmp(&new_cfg, &now_cfg, sizeof(struct ssd_cfg))) {
		ret = ssd_save_config(sdev, &new_cfg);
		if(ret)
			printk(KERN_WARNING "%s: save config error.\n", sdev->name);
	}

	ssd_card_table[idx]->stripe_size = new_cfg.stripe_size;
	ssd_card_table[idx]->rb_percent  = new_cfg.rb_percent;

	if(!rb_compatibility) {
		data_page_count = sdev->fpga_info.page_count - SSD_RESERVED_PAGES_PER_BLOCKS;
		if(ssd_card_table[idx]->version & 0x1) {
			if(ssd_card_table[idx]->version >= FPGA_VER_NEW) { // NEW VER CONTROLLER FOR NEW CHIPS
				if(sdev->fpga_info.page_count == 256) { // NEW MICRON/INTEL FLASH
					if(ssd_card_table[idx]->version >= FPGA_VER_NEW_L2) {
						data_page_count -= SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH_L2;
					} else if(ssd_card_table[idx]->version >= FPGA_VER_NEW_L1) {
						data_page_count -= SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH_L1;
					} else {
						data_page_count -= SSD_MI_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH;
					}
				} else {
					data_page_count -= SSD_128P_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH;
				}
			}
			else if(ssd_card_table[idx]->type == SSD_TYPE_MLC) {
				data_page_count -= SSD_MLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH;
			} else {
				data_page_count -= SSD_SLC_RESERVED_PAGES_PER_BLOCKS_NEW_ARITH;
			}
		}

		list_for_each_entry(itor, &(ssd_card_table[idx]->ssd_list), list) {
			ssd_reg32_write(itor->ctrlp + FPGA_STRIPED_REG, new_cfg.stripe_size);
			rb = itor->fpga_info.block_count * (100-new_cfg.rb_percent);
			rb /= 100;
			rb = (rb-1) * itor->fpga_info.flash_count;
			itor->size = ((uint64_t)(rb * data_page_count)) << itor->fpga_info.page_shift;
		}
	} else {
		data_page_count = sdev->fpga_info.page_count;
		list_for_each_entry(itor, &(ssd_card_table[idx]->ssd_list), list) {
			ssd_reg32_write(itor->ctrlp + FPGA_STRIPED_REG, new_cfg.stripe_size);
			rb = itor->fpga_info.block_count * (100-new_cfg.rb_percent);
			rb /= 100;
			rb = rb * itor->fpga_info.flash_count;
			itor->size = ((uint64_t)(rb * data_page_count)) << itor->fpga_info.page_shift;
		}
	}
out:
	return ret;
}


static int add_to_card(struct ssd_device* sdev)
{
	int i;
	uint32_t val;

	for(i=0; i<MAX_CARD; i++) {
		if(sdev->pdev->bus->primary == ssd_card_table[i]->bus) {
			break;
		}
	}

	if(i==MAX_CARD || (ssd_card_table[i]->expect_dev_count == ssd_card_table[i]->dev_count)) {
		for(i=0; i<MAX_CARD; i++) {
			if(0 == ssd_card_table[i]->bus) {
				val = ssd_reg32_read(sdev->ctrlp+CARD_TYPE_REG);
				if(val > SSD_CARD_TYPE2) {
					printk(KERN_WARNING "%s: unknown type!\n", sdev->name);
					return -1;
				}
				ssd_card_table[i]->expect_dev_count = ssd_cardtype_map[val];
				ssd_card_table[i]->bus = sdev->pdev->bus->primary;
				ssd_card_table[i]->type = sdev->fpga_info.page_size;
				ssd_card_table[i]->version = sdev->fpga_ver;
				if(unlikely(ssd_card_table[i]->type != SSD_TYPE_MLC && 
					ssd_card_table[i]->type != SSD_TYPE_SLC)) {
					printk(KERN_WARNING "%s: unknown chip type!\n", sdev->name);
					return -1;
				}
				ssd_card_count++;
				
				break;
			}
		}
	}

	if(i==MAX_CARD) {
		printk(KERN_WARNING "%s: too many ssd.\n", sdev->name);
		return -1;
	}

	if(unlikely(sdev->fpga_info.page_size != ssd_card_table[i]->type)) {
		printk(KERN_WARNING "%s: chip type mismatch!", sdev->name);
		ssd_card_table[i]->status = 1;
	}

	if(ssd_card_table[i]->controller_size == 0) {
		ssd_card_table[i]->controller_size = sdev->fpga_info.phy_size;
	} else {
		if(ssd_card_table[i]->controller_size != sdev->fpga_info.phy_size) {
			printk(KERN_WARNING "%s: ssd size mismatch!\n", sdev->name);
			ssd_card_table[i]->status = 1;
		}
	}
	ssd_card_table[i]->size += sdev->fpga_info.phy_size;
	
	val = ssd_reg32_read(sdev->ctrlp+CPLD_VER_REG);
	if(!(val & 0x4)) {
		ssd_card_table[i]->cpld_ver = val & 0x3;
		val = ssd_reg32_read(sdev->ctrlp+PCB_VER_REG);
		ssd_card_table[i]->pcb_ver = (uint8_t) ((val >> 3) & 0x7);
		ssd_card_table[i]->upper_pcb_ver = (uint8_t) (val & 0x7);
		ssd_card_table[i]->primary = sdev->controller_idx;
		ssd_card_table[i]->primary_dev = sdev;
	}

	ssd_card_table[i]->dev_count++;
	list_add_tail(&(sdev->list), &(ssd_card_table[i]->ssd_list));
	sdev->parent = (void *)(ssd_card_table[i]);

	return 0;
}

static int ssd_init_one(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct ssd_device *sdev;
	int ret = 0,irq,i, fpgaver;

#if 0
	if(ssd_controller_count > 0 )
	{
	return 0; 
	}
#endif
	sdev = kmalloc(sizeof(struct ssd_device), GFP_KERNEL);
	if (!sdev)  {
		dev_err(&pdev->dev, "%s: memory alloc failure\n", sdev->name);
		ret = -ENOMEM;
		goto err_out_nomem;
	}
	memset(sdev, 0, sizeof(struct ssd_device));

	sdev->controller_idx = ssd_controller_count;
	sprintf(sdev->name, DEV_NAME "%c", 'a'+sdev->controller_idx);
	ssd_controller_count++;

	if (pci_enable_device(pdev))
		goto err_out_enable_device;

	pci_set_master(pdev);

	if (!request_mem_region(pci_resource_start(pdev, 0),
		pci_resource_len(pdev, 0), DEV_NAME)) {
		dev_err(&pdev->dev, "%s: cannot reserve MMIO region 0\n", sdev->name);
		goto err_out_request_mem_region;
	}

	irq = pdev->irq;
//	printk(KERN_INFO "Probing SSD device at %#x, IRQ %d\n", (uint32_t)pci_resource_start(pdev, 0), irq);


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	if (pci_set_dma_mask(pdev, DMA_64BIT_MASK))
#else
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
#endif
		printk(KERN_WARNING "%s: no 64 bits DMA available.\n", sdev->name);

	sdev->pdev = pdev;
	//	spin_lock_init(&sdev->sdev_lock);
	//       spin_lock_init(&sdev->map_lock);
	//       spin_lock_init(&sdev->ioctl_lock);
	//       spin_lock_init(&sdev->seq_lock);

	sdev->ctrlp = pci_iomap(pdev, 0, 0);
	if(!sdev->ctrlp) {
		printk(KERN_WARNING "%s: cannot remap IO region 0\n", sdev->name);
		goto err_out_ioremap;
	}


	fpgaver= ssd_reg32_read(sdev->ctrlp + FPGA_VER_REG);

	if (fpgaver < FPGA_VERSION_MIN || fpgaver > FPGA_VERSION_MAX) {
		printk(KERN_WARNING "SSD Controller version is %03X, but the driver supports %x to %x.\n",
		fpgaver,FPGA_VERSION_MIN, FPGA_VERSION_MAX);
		goto err_out_fpgaver;
	}
	//fpga_ver[sdev->controller_idx] = fpgaver;
	sdev->fpga_ver = fpgaver;
	printk(KERN_INFO "SSD controller version is %03X\n", sdev->fpga_ver);
/*
	if(!check_nor(sdev)) {
		cpld_ver[nor_num] = read_cpld_ver(sdev);
		read_pcb_ver(sdev, &pcb_ver[nor_num].board_pcb_ver, &pcb_ver[nor_num].upper_board_pcb_ver);
		nor_fpga[nor_num] = sdev->controller_idx;
		nor_num++;
	}
*/
	//ret = ssd_get_fpga_stat(sdev);
	ret = ssd_check_init_state(sdev);
	if(ret) {		
		printk(KERN_WARNING "%s: controller init state error!\n", sdev->name);
		if(mode != SSD_DRV_MODE_DEBUG) {
			goto err_out_get_fpga_stat;
		}
	}

	ret = ssd_init_msg(sdev);
	if (ret) {
		printk(KERN_WARNING "%s: init msg failded\n", sdev->name);
		goto err_out_init_msg;
	}

	ret = ssd_init_responses(sdev);
	if (ret) {
		printk(KERN_WARNING "%s: init resp msg failded\n", sdev->name);
		goto err_out_init_responses;
	}

	//	list_add_tail(&sdev->list, &sdev_list);

	//init_waitqueue_head(&sdev->log_thread_wait_q);

	ssd_init_fpga_info(sdev);

	//init_MUTEX(&sdev->log_w_sema);
	sema_init(&sdev->bbt_sema, 1);


	//spin_lock_init(&sdev->log_lock);
	//INIT_LIST_HEAD(&sdev->log_list_ssd);

	sdev->workqueue = create_singlethread_workqueue(sdev->name);
	if(NULL == sdev->workqueue)
		goto err_out_create_workqueue;
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	INIT_WORK(&sdev->read_wk,ssd_get_log,sdev);
	INIT_WORK(&sdev->flush_wk, ssd_flush_work, sdev);
#else
	INIT_WORK(&sdev->read_wk,ssd_get_log);
	INIT_WORK(&sdev->flush_wk, ssd_flush_work);
#endif
	
	pci_set_drvdata(pdev, sdev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	if (request_irq(pdev->irq, &ssd_interrupt, SA_SHIRQ, sdev->name, sdev)) {
#else
	if (request_irq(pdev->irq, &ssd_interrupt, IRQF_SHARED, sdev->name, sdev)) {
#endif
		printk(KERN_WARNING "%s: irq register failed 0\n", sdev->name);
		goto err_out_request_irq;
	}

	sdev->owner = THIS_MODULE;
	
	ssd_table[sdev->controller_idx] = sdev;
	add_to_card(sdev);

	//ssd_reg_write(sdev->ctrlp + FPGA_INT_INTERVAL_REG, 0x8232);
	//printk("FPGA_INT_INTERVAL_REG=%llx\n", ssd_reg_read(sdev->ctrlp + FPGA_INT_INTERVAL_REG));
//	ret = add_SSD(sdev);

	ssd_init_ok_sum++;

	return 0;

err_out_request_irq:
	//if(sdev->filp)
		//filp_close(sdev->filp, current->files);
err_out_create_workqueue:
	pci_free_consistent(sdev->pdev,8,sdev->resp_point_addr,sdev->resp_point_dma_addr);
	pci_free_consistent(sdev->pdev,sizeof(struct ssd_resp_msg)*SSD_MAX_RESP_Q_LEN,sdev->resp_stat_addr_base,sdev->resp_stat_dma_addr);
err_out_init_responses:
	for(i = 0;i < SSD_CMD_FIFO_DEPTH ;i++)
		pci_free_consistent(sdev->pdev,sizeof(struct ssd_rw_msg),sdev->dmatable_cpu[i],sdev->dmatable_dma[i]);
err_out_init_msg:
err_out_get_fpga_stat:
err_out_fpgaver:
#ifdef LINUX_SUSE_OS
	iounmap(sdev->ctrlp);
#else
	pci_iounmap(pdev, sdev->ctrlp);
#endif
err_out_ioremap:
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_out_request_mem_region:
	pci_disable_device(pdev);
err_out_enable_device:
	kfree(sdev);
	sdev = NULL;
err_out_nomem:
	return -ENODEV;
}

static void ssd_remove_one (struct pci_dev *pdev)
{
	struct ssd_device *sdev = pci_get_drvdata (pdev);
	uint32_t val;
	int i;

	if(!sdev) {
		printk("error, sdev is NULL\n");
		return ;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	cancel_delayed_work(&sdev->read_wk);
#endif
	//flush_workqueue(sdev->workqueue);
	destroy_workqueue(sdev->workqueue);
	sdev->workqueue = NULL;

	free_irq(pdev->irq, sdev);

#if 0
	if(sdev->filp) {
		//fput(sdev->filp);
		filp_close(sdev->filp, current->files);
	}
#endif

	pci_free_consistent(sdev->pdev,8,sdev->resp_point_addr,sdev->resp_point_dma_addr);
	pci_free_consistent(sdev->pdev,sizeof(struct ssd_resp_msg)*SSD_MAX_RESP_Q_LEN,sdev->resp_stat_addr_base,sdev->resp_stat_dma_addr);

	for(i = 0;i < SSD_CMD_FIFO_DEPTH ;i++)
		pci_free_consistent(sdev->pdev,sizeof(struct ssd_rw_msg),sdev->dmatable_cpu[i],sdev->dmatable_dma[i]);    

	if(reload_fpga == 1) { //reload fpga
		val = ssd_reg32_read(sdev->ctrlp+CPLD_VER_REG);
		if(!(val & 0x4)) {
			ssd_reg32_write(sdev->ctrlp+CPLD_VER_REG, 0x8);
		}
	}

	/* unmap physical adress */
#ifdef LINUX_SUSE_OS
	iounmap(sdev->ctrlp);
#else
	pci_iounmap(pdev, sdev->ctrlp);
#endif

	
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));

	pci_set_drvdata(pdev,NULL) ; 

	kfree(sdev);

	pci_disable_device(pdev);
}


static struct pci_device_id ssd_pci_tbl[] = {
       #define PCI_VENDOR_ID_SSD_ADV 0x1172
	{ PCI_VENDOR_ID_SSD_ADV, 0x0005, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, ssd_pci_tbl);

static struct pci_driver ssd_driver = {
	.name		= MODULE_NAME,
	.id_table	= ssd_pci_tbl,
	.probe		= ssd_init_one,
	.remove		= ssd_remove_one,
};

static int ssd_notify_reboot(struct notifier_block *nb, unsigned long event, void *buf)
{
	int i=0;
	struct ssd_device *sdev = NULL;
	for (i=0; i<ssd_controller_count; i++) {
		if(ssd_table[i]) {
			//sync_blockdev(bdget_disk(ssd_table[i]->gd, 0));
			ssd_flush(ssd_table[i]);
		}
	}

	for(i = 0; i < ssd_card_count; i++) {
		if(ssd_card_table[i]->bus)
			sdev = ssd_card_table[i]->primary_dev;
		
		if(reload_fpga == 1) { //reload fpga
				ssd_reg32_write(sdev->ctrlp+CPLD_VER_REG, 0x8);
		}
	}
	return NOTIFY_OK;
}

/* notifier block to get a notify on system shutdown/halt/reboot */
static struct notifier_block ssd_notifier = {
	ssd_notify_reboot, NULL, 0
};

static int __init ssd_init_module(void)
{
	int i;
	int ret = 0;
	
    printk(KERN_INFO "SSD: Load SSD Module, Driver Version is %s\n", DRIVER_VERSION);
	ssd_card_table[0] = kmalloc(MAX_CARD*sizeof(struct ssd_card), GFP_KERNEL);
	if (!ssd_card_table[0])	{
		printk(KERN_WARNING "SSD: memory alloc failure!");
		ret = -ENOMEM;
		goto out;
	}
	memset(ssd_card_table[0], 0, MAX_CARD*sizeof(struct ssd_card));
	INIT_LIST_HEAD(&(ssd_card_table[0]->ssd_list));
	for(i=1;i<MAX_CARD;i++) {
		ssd_card_table[i] = ssd_card_table[0] + i;
		INIT_LIST_HEAD(&(ssd_card_table[i]->ssd_list));
	}

#ifdef CONFIG_PROC_FS
	if ((proc_ssd = proc_mkdir(SSD_PROC_DIR, NULL)) == NULL)
		goto out_err_proc_mkdir;

	if ((proc_info = proc_create("info", S_IFREG | S_IRUGO | S_IWUSR, proc_ssd, &proc_info_fops)) == NULL)
		goto out_err_proc_info;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	proc_info->owner = THIS_MODULE;
#endif


#endif

	for(i = 0;i < MAX_FPGA_NUM;i++)
		ssd_table[i] = NULL;
	
	ret = ssd_major = register_blkdev(ssd_major, DEV_NAME);
	if (ssd_major <= 0) {
		printk(KERN_WARNING "SSD: unable to get major number\n");
		goto out_err_register_blkdev;
	}
	for_each_online_cpu(i) {
		INIT_LIST_HEAD(&per_cpu(ssd_done_q, i));

		tasklet_init(&per_cpu(ssd_tasklet, i), ssd_end_request, 0);
	}

	sema_init(&log_mutex, 1);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	ret = pci_module_init(&ssd_driver);
#else
	ret = pci_register_driver(&ssd_driver);
#endif

	if(ret != 0) {
		printk(KERN_WARNING "SSD: pci init error!\n");
		goto out_err_pci_init;
	}

	if(ssd_controller_count == 0) {
		printk(KERN_WARNING "SSD: no device found!\n");
		ret = -ENODEV;
		goto out_err_no_device;
	}

	if(0 == ssd_init_ok_sum && mode != SSD_DRV_MODE_DEBUG) {
		printk(KERN_WARNING "SSD: all of ssd device init error\n");
		ret = -ENODEV;
		goto out_err_device_miss;
	}

	for(i=0; i<ssd_card_count; i++)	{
		if(ssd_card_table[i]->status != 0 ||
			ssd_card_table[i]->dev_count != ssd_card_table[i]->expect_dev_count) {
			show_ssd(i);
		}
	}

	for(i=0; i<ssd_card_count; i++)	{
		ret = ssd_config(i);
		if((ret) && mode != SSD_DRV_MODE_DEBUG) {
			printk(KERN_WARNING "SSD %d: set config error.\n", i);
			goto out_err_ssd_config;
		}
	}

	for(i=0; i<ssd_controller_count; i++)	{
		if(ssd_table[i] != NULL) {
			ret = add_SSD(ssd_table[i]);
			if(ret)
				goto err_aad_SSD;
		}
	}

	register_reboot_notifier(&ssd_notifier);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(1000);
	return 0;

err_aad_SSD:
	for(i--; i>=0; i--) {
		del_SSD(ssd_table[i]);
	}
	
out_err_ssd_config:
out_err_device_miss:
out_err_no_device:
out_err_pci_init:
	pci_unregister_driver(&ssd_driver);
	for_each_online_cpu(i) {
/*		while(!list_empty(&per_cpu(ssd_done_q, i))) {
			schedule_timeout(100);	//wait for end
		}*/
		tasklet_kill(&per_cpu(ssd_tasklet, i));
	}
	unregister_blkdev(ssd_major, DEV_NAME);
out_err_register_blkdev:
#ifdef CONFIG_PROC_FS
	remove_proc_entry("info", proc_ssd);
out_err_proc_info:
	remove_proc_entry(SSD_PROC_DIR, NULL);
out_err_proc_mkdir:
#endif
out:
	return ret;

}

static void __exit ssd_cleanup_module(void)
{
	int i;

	unregister_reboot_notifier(&ssd_notifier);
	not_deal_log = 1;
	if(log_pid  != 0)
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,26))
		kill_proc(log_pid, SIGKILL, 22);
#else
		kill_pid(find_vpid(log_pid), SIGKILL, 22);
#endif

	for(i=0; i<ssd_controller_count; i++)	{
		if(ssd_table[i] != NULL)
			del_SSD(ssd_table[i]);
	}

	pci_unregister_driver(&ssd_driver);

	for_each_online_cpu(i) {
/*		while(!list_empty(&per_cpu(ssd_done_q, i))) {
			printk(KERN_WARNING "cpu %d list not empty\n", i);
			schedule_timeout(100);	//wait for end
		}*/
		tasklet_kill(&per_cpu(ssd_tasklet, i));
	}
	unregister_blkdev(ssd_major, DEV_NAME);

#ifdef CONFIG_PROC_FS
	if (proc_info)
		remove_proc_entry("info", proc_ssd);
	if (proc_ssd) 
		remove_proc_entry(SSD_PROC_DIR, NULL);
#endif
	
	kfree(ssd_card_table[0]);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(1000);
	printk(KERN_INFO "SSD: unload SSD Module\n");
}

module_init(ssd_init_module);
module_exit(ssd_cleanup_module);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("SSD DEV Team");
MODULE_DESCRIPTION("SSD driver");
