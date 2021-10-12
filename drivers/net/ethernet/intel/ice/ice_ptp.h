/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_classify.h>
#include <linux/highuid.h>

#include "ice_ptp_hw.h"

enum ice_ptp_pin {
	GPIO_20 = 0,
	GPIO_21,
	GPIO_22,
	GPIO_23,
	NUM_ICE_PTP_PIN
};


#define ICE_E810T_SMA1_CTRL_MASK	(ICE_E810T_P1_SMA1_DIR_EN | \
						ICE_E810T_P1_SMA1_TX_EN)
#define ICE_E810T_SMA2_CTRL_MASK	(ICE_E810T_P1_SMA2_UFL2_RX_DIS | \
						ICE_E810T_P1_SMA2_DIR_EN | \
						ICE_E810T_P1_SMA2_TX_EN)
#define ICE_E810T_SMA_CTRL_MASK		(ICE_E810T_SMA1_CTRL_MASK | \
						ICE_E810T_SMA2_CTRL_MASK)

enum ice_e810t_ptp_pins {
	GNSS = 0,
	SMA1,
	UFL1,
	SMA2,
	UFL2,
	NUM_E810T_PTP_PINS
};

#define ICE_SUBDEV_ID_E810_T 0x000E

static inline bool ice_is_e810t(struct ice_hw *hw)
{
	return (hw->device_id == ICE_DEV_ID_E810C_SFP &&
		hw->subsystem_device_id == ICE_SUBDEV_ID_E810_T);
}

struct ice_perout_channel {
	bool ena;
	u32 gpio_pin;
	u64 period;
	u64 start_time;
};


/**
 * struct ice_ptp_port - data used to initialize an external port for PTP
 *
 * This structure contains data indicating whether a single external port is
 * ready for PTP functionality. It is used to track the port initialization
 * and determine when the port's PHY offset is valid.
 *
 * @ov_task: work task for tracking when PHY offset is valid
 * @tx_offset_ready: indicates the Tx offset for the port is ready
 * @rx_offset_ready: indicates the Rx offset for the port is ready
 * @tx_offset_lock: lock used to protect the tx_offset_ready field
 * @rx_offset_lock: lock used to protect the rx_offset_ready field
 * @ps_lock: mutex used to protect the overall PTP PHY start procedure
 * @link_up: indicates whether the link is up
 * @tx_fifo_busy_cnt: number of times the Tx FIFO was busy
 * @port_num: the port number this structure represents
 */
struct ice_ptp_port {
	struct work_struct ov_task;
	atomic_t tx_offset_ready;
	atomic_t rx_offset_ready;
	atomic_t tx_offset_lock;
	atomic_t rx_offset_lock;
	struct mutex ps_lock; /* protects overall PTP PHY start procedure */
	bool link_up;
	u8 tx_fifo_busy_cnt;
	u8 port_num;
};

#define GLTSYN_TGT_H_IDX_MAX		4

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @port: data for the PHY port initialization procedure
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @ext_ts_chan: the external timestamp channel in use
 * @ext_ts_irq: the external timestamp IRQ in use
 * @phy_reset_lock: bit lock for preventing PHY start while resetting
 * @ov_wq: work queue for the offset validity task
 * @perout_channels: periodic output data
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @tstamp_config: hardware timestamping configuration
 * @time_ref_freq: current device timer frequency (for E822 devices)
 * @src_tmr_mode: current device timer mode (locked or nanoseconds)
 */
struct ice_ptp {
	struct ice_ptp_port port;
	u64 cached_phc_time;
	u8 ext_ts_chan;
	u8 ext_ts_irq;
	atomic_t phy_reset_lock;
	struct workqueue_struct *ov_wq;
	struct ice_perout_channel perout_channels[GLTSYN_TGT_H_IDX_MAX];
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct hwtstamp_config tstamp_config;
	enum ice_time_ref_freq time_ref_freq;
	enum ice_src_tmr_mode src_tmr_mode;
};

#define __ptp_port_to_ptp(p) \
	container_of((p), struct ice_ptp, port)
#define ptp_port_to_pf(p) \
	container_of(__ptp_port_to_ptp((p)), struct ice_pf, ptp)

#define __ptp_info_to_ptp(i) \
	container_of((i), struct ice_ptp, info)
#define ptp_info_to_pf(i) \
	container_of(__ptp_info_to_ptp((i)), struct ice_pf, ptp)

#define MAC_RX_LINK_COUNTER(_port)	(0x600090 + 0x1000 * (_port))
#define PFTSYN_SEM_BYTES		4
#define PTP_SHARED_CLK_IDX_VALID	BIT(31)
#define PHY_TIMER_SELECT_VALID_BIT	0
#define PHY_TIMER_SELECT_BIT		1
#define PHY_TIMER_SELECT_MASK		0xFFFFFFFC
#define TS_CMD_MASK_EXT			0xFF
#define TS_CMD_MASK			0xF
#define SYNC_EXEC_CMD			0x3
#define ICE_PTP_TS_VALID		BIT(0)
#define FIFO_EMPTY			BIT(2)
#define FIFO_OK				0xFF
#define ICE_PTP_FIFO_NUM_CHECKS		5
/* PHY, quad and port definitions */
#define INDEX_PER_QUAD			64
#define INDEX_PER_PORT			(INDEX_PER_QUAD / ICE_PORTS_PER_QUAD)
#define TX_INTR_QUAD_MASK		0x03
/* Per-channel register definitions */
#define GLTSYN_AUX_OUT(_chan, _idx)	(GLTSYN_AUX_OUT_0(_idx) + ((_chan) * 8))
#define GLTSYN_AUX_IN(_chan, _idx)	(GLTSYN_AUX_IN_0(_idx) + ((_chan) * 8))
#define GLTSYN_CLKO(_chan, _idx)	(GLTSYN_CLKO_0(_idx) + ((_chan) * 8))
#define GLTSYN_TGT_L(_chan, _idx)	(GLTSYN_TGT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_TGT_H(_chan, _idx)	(GLTSYN_TGT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_L(_chan, _idx)	(GLTSYN_EVNT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H(_chan, _idx)	(GLTSYN_EVNT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H_IDX_MAX		3

/* Pin definitions for PTP PPS out */
#define PPS_CLK_GEN_CHAN		3
#define PPS_CLK_SRC_CHAN		2
#define PPS_PIN_INDEX			5
#define TIME_SYNC_PIN_INDEX		4
#define E810_N_EXT_TS			3
#define E810_N_PER_OUT			4
#define E810T_N_PER_OUT			3
/* Macros to derive the low and high addresses for PHY */
#define LOWER_ADDR_SIZE			16
/* Macros to derive offsets for TimeStampLow and TimeStampHigh */
#define PORT_TIMER_ASSOC(_i)		(0x0300102C + ((_i) * 256))
#define ETH_GLTSYN_ENA(_i)		(0x03000348 + ((_i) * 4))

/* Time allowed for programming periodic clock output */
#define START_OFFS_NS 100000000

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
struct ice_pf;
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_idx(struct ice_vsi *vsi);
int ice_get_ptp_clock_index(struct ice_pf *pf);

void ice_clean_ptp_subtask(struct ice_pf *pf);
void ice_ptp_set_timestamp_offsets(struct ice_pf *pf);
u64
ice_ptp_read_src_clk_reg(struct ice_pf *pf, struct ptp_system_timestamp *sts);
void ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring, union ice_32b_rx_flex_desc *rx_desc,
			 struct sk_buff *skb);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
int ice_ptp_link_change(struct ice_pf *pf, u8 port, bool linkup);
int ice_ptp_check_rx_fifo(struct ice_pf *pf, u8 port);
int ptp_ts_enable(struct ice_pf *pf, u8 port, bool enable);
int ice_ptp_cfg_clkout(struct ice_pf *pf, unsigned int chan,
		       struct ice_perout_channel *config, bool store);
int ice_ptp_update_incval(struct ice_pf *pf, enum ice_time_ref_freq time_ref_freq,
			  enum ice_src_tmr_mode src_tmr_mode);
int ice_ptp_get_incval(struct ice_pf *pf, enum ice_time_ref_freq *time_ref_freq,
		       enum ice_src_tmr_mode *src_tmr_mode);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_ptp_set_ts_config(struct ice_pf __always_unused *pf,
					struct ifreq __always_unused *ifr)
{
	return 0;
}

static inline int ice_ptp_get_ts_config(struct ice_pf __always_unused *pf,
					struct ifreq __always_unused *ifr)
{
	return 0;
}

static inline int
ice_ptp_check_rx_fifo(struct ice_pf __always_unused *pf,
		      u8 __always_unused port)
{
	return 0;
}

static inline int ice_ptp_get_ts_idx(struct ice_vsi __always_unused *vsi)
{
	return 0;
}

static inline int ice_get_ptp_clock_index(struct ice_pf __always_unused *pf)
{
	return 0;
}
static inline void ice_clean_ptp_subtask(struct ice_pf *pf) { }
static inline void ice_ptp_set_timestamp_offsets(struct ice_pf *pf) { }
static inline void ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
				       union ice_32b_rx_flex_desc *rx_desc,
				       struct sk_buff *skb) { }
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
static inline int ice_ptp_link_change(struct ice_pf *pf, u8 port, bool linkup)
{ return 0; }
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */
