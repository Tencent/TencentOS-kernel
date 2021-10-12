// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

#define E810_OUT_PROP_DELAY_NS 1


#define LOCKED_INCVAL_E822 0x100000000ULL

static const struct ptp_pin_desc ice_e810t_pin_desc[] = {
	/* name     idx   func         chan */
	 { "GNSS",  GNSS, PTP_PF_EXTTS, 0, { 0, } },
	 { "SMA1",  SMA1, PTP_PF_NONE, 1, { 0, } },
	 { "U.FL1", UFL1, PTP_PF_NONE, 1, { 0, } },
	 { "SMA2",  SMA2, PTP_PF_NONE, 2, { 0, } },
	 { "U.FL2", UFL2, PTP_PF_NONE, 2, { 0, } },
};

/**
 * ice_enable_e810t_sma_ctrl
 * @hw: pointer to the hw struct
 * @ena: set true to enable and false to disable
 *
 * Enables or disable the SMA control logic
 */
static int ice_enable_e810t_sma_ctrl(struct ice_hw *hw, bool ena)
{
	int err;
	u8 data;

	/* Set expander bits as outputs */
	err = ice_read_e810t_pca9575_reg(hw, ICE_PCA9575_P1_CFG, &data);
	if (err)
		return err;

	if (ena)
		data &= (~ICE_E810T_SMA_CTRL_MASK);
	else
		data |= ICE_E810T_SMA_CTRL_MASK;

	return ice_write_e810t_pca9575_reg(hw, ICE_PCA9575_P1_CFG, data);
}

/**
 * ice_get_e810t_sma_config
 * @hw: pointer to the hw struct
 * @ptp_pins:pointer to the ptp_pin_desc struture
 *
 * Read the configuration of the SMA control logic and put it into the
 * ptp_pin_desc structure
 */
static int
ice_get_e810t_sma_config(struct ice_hw *hw, struct ptp_pin_desc *ptp_pins)
{
	enum ice_status status;
	u8 data, i;

	/* Read initial pin state */
	status = ice_read_e810t_pca9575_reg(hw, ICE_PCA9575_P1_OUT, &data);
	if (status)
		return ice_status_to_errno(status);

	/* initialize with defaults */
	for (i = 0; i < NUM_E810T_PTP_PINS; i++) {
		snprintf(ptp_pins[i].name, sizeof(ptp_pins[i].name),
			 "%s", ice_e810t_pin_desc[i].name);
		ptp_pins[i].index = ice_e810t_pin_desc[i].index;
		ptp_pins[i].func = ice_e810t_pin_desc[i].func;
		ptp_pins[i].chan = ice_e810t_pin_desc[i].chan;
	}

	/* Parse SMA1/UFL1 */
	switch (data & ICE_E810T_SMA1_CTRL_MASK) {
	case ICE_E810T_SMA1_CTRL_MASK:
	default:
		ptp_pins[SMA1].func = PTP_PF_NONE;
		ptp_pins[UFL1].func = PTP_PF_NONE;
		break;
	case ICE_E810T_P1_SMA1_DIR_EN:
		ptp_pins[SMA1].func = PTP_PF_PEROUT;
		ptp_pins[UFL1].func = PTP_PF_NONE;
		break;
	case ICE_E810T_P1_SMA1_TX_EN:
		ptp_pins[SMA1].func = PTP_PF_EXTTS;
		ptp_pins[UFL1].func = PTP_PF_NONE;
		break;
	case 0:
		ptp_pins[SMA1].func = PTP_PF_EXTTS;
		ptp_pins[UFL1].func = PTP_PF_PEROUT;
		break;
	}

	/* Parse SMA2/UFL2 */
	switch (data & ICE_E810T_SMA2_CTRL_MASK) {
	case ICE_E810T_SMA2_CTRL_MASK:
	default:
		ptp_pins[SMA2].func = PTP_PF_NONE;
		ptp_pins[UFL2].func = PTP_PF_NONE;
		break;
	case (ICE_E810T_P1_SMA2_TX_EN | ICE_E810T_P1_SMA2_UFL2_RX_DIS):
		ptp_pins[SMA2].func = PTP_PF_EXTTS;
		ptp_pins[UFL2].func = PTP_PF_NONE;
		break;
	case (ICE_E810T_P1_SMA2_DIR_EN | ICE_E810T_P1_SMA2_UFL2_RX_DIS):
		ptp_pins[SMA2].func = PTP_PF_PEROUT;
		ptp_pins[UFL2].func = PTP_PF_NONE;
		break;
	case (ICE_E810T_P1_SMA2_DIR_EN | ICE_E810T_P1_SMA2_TX_EN):
		ptp_pins[SMA2].func = PTP_PF_NONE;
		ptp_pins[UFL2].func = PTP_PF_EXTTS;
		break;
	case ICE_E810T_P1_SMA2_DIR_EN:
		ptp_pins[SMA2].func = PTP_PF_PEROUT;
		ptp_pins[UFL2].func = PTP_PF_EXTTS;
		break;
	}

	return 0;
}

/**
 * ice_ptp_set_e810t_sma_state
 * @hw: pointer to the hw struct
 * @ptp_pins: pointer to the ptp_pin_desc struture
 *
 * Set the configuration of the SMA control logic based on the configuration in
 * num_pins parameter
 */
static int
ice_ptp_set_e810t_sma_state(struct ice_hw *hw,
			    const struct ptp_pin_desc *ptp_pins)
{
	enum ice_status status;
	u8 data;

	/* SMA1 and UFL1 cannot be set to TX at the same time */
	if (ptp_pins[SMA1].func == PTP_PF_PEROUT &&
	    ptp_pins[UFL1].func == PTP_PF_PEROUT)
		return ICE_ERR_PARAM;

	/* SMA2 and UFL2 cannot be set to RX at the same time */
	if (ptp_pins[SMA2].func == PTP_PF_EXTTS &&
	    ptp_pins[UFL2].func == PTP_PF_EXTTS)
		return ICE_ERR_PARAM;

	/* Read initial pin state value */
	status = ice_read_e810t_pca9575_reg(hw, ICE_PCA9575_P1_OUT, &data);
	if (status)
		return ice_status_to_errno(status);

	/* Set the right sate based on the desired configuration */
	data &= ~ICE_E810T_SMA1_CTRL_MASK;
	if (ptp_pins[SMA1].func == PTP_PF_NONE &&
	    ptp_pins[UFL1].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA1 + U.FL1 disabled");
		data |= ICE_E810T_SMA1_CTRL_MASK;
	} else if (ptp_pins[SMA1].func == PTP_PF_EXTTS &&
		   ptp_pins[UFL1].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA1 RX");
		data |= ICE_E810T_P1_SMA1_TX_EN;
	} else if (ptp_pins[SMA1].func == PTP_PF_NONE &&
		   ptp_pins[UFL1].func == PTP_PF_PEROUT) {
		/* U.FL 1 TX will always enable SMA 1 RX */
		dev_info(ice_hw_to_dev(hw), "SMA1 RX + U.FL1 TX");
	} else if (ptp_pins[SMA1].func == PTP_PF_EXTTS &&
		   ptp_pins[UFL1].func == PTP_PF_PEROUT) {
		dev_info(ice_hw_to_dev(hw), "SMA1 RX + U.FL1 TX");
	} else if (ptp_pins[SMA1].func == PTP_PF_PEROUT &&
		   ptp_pins[UFL1].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA1 TX");
		data |= ICE_E810T_P1_SMA1_DIR_EN;
	}

	data &= (~ICE_E810T_SMA2_CTRL_MASK);
	if (ptp_pins[SMA2].func == PTP_PF_NONE &&
	    ptp_pins[UFL2].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA2 + U.FL2 disabled");
		data |= ICE_E810T_SMA2_CTRL_MASK;
	} else if (ptp_pins[SMA2].func == PTP_PF_EXTTS &&
			ptp_pins[UFL2].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA2 RX");
		data |= (ICE_E810T_P1_SMA2_TX_EN |
			 ICE_E810T_P1_SMA2_UFL2_RX_DIS);
	} else if (ptp_pins[SMA2].func == PTP_PF_NONE &&
		   ptp_pins[UFL2].func == PTP_PF_EXTTS) {
		dev_info(ice_hw_to_dev(hw), "UFL2 RX");
		data |= (ICE_E810T_P1_SMA2_DIR_EN | ICE_E810T_P1_SMA2_TX_EN);
	} else if (ptp_pins[SMA2].func == PTP_PF_PEROUT &&
		   ptp_pins[UFL2].func == PTP_PF_NONE) {
		dev_info(ice_hw_to_dev(hw), "SMA2 TX");
		data |= (ICE_E810T_P1_SMA2_DIR_EN |
			 ICE_E810T_P1_SMA2_UFL2_RX_DIS);
	} else if (ptp_pins[SMA2].func == PTP_PF_PEROUT &&
		   ptp_pins[UFL2].func == PTP_PF_EXTTS) {
		dev_info(ice_hw_to_dev(hw), "SMA2 TX + U.FL2 RX");
		data |= ICE_E810T_P1_SMA2_DIR_EN;
	}

	status = ice_write_e810t_pca9575_reg(hw, ICE_PCA9575_P1_OUT, data);
	if (status)
		return ice_status_to_errno(status);

	return 0;
}

/**
 * ice_ptp_set_e810t_sma
 * @info: the driver's PTP info structure
 * @pin: pin index in kernel structure
 * @func: Pin function to be set (PTP_PF_NONE, PTP_PF_EXTTS or PTP_PF_PEROUT)
 *
 * Set the configuration of a single SMA pin
 */
static int
ice_ptp_set_e810t_sma(struct ptp_clock_info *info, unsigned int pin,
		      enum ptp_pin_function func)
{
	struct ptp_pin_desc ptp_pins[NUM_E810T_PTP_PINS];
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;
	int err;

	if (pin < SMA1 || func > PTP_PF_PEROUT)
		return -EOPNOTSUPP;

	err = ice_get_e810t_sma_config(hw, ptp_pins);
	if (err)
		return err;

	/* Disable the same function on the other pin sharing the channel */
	if (pin == SMA1 && ptp_pins[UFL1].func == func)
		ptp_pins[UFL1].func = PTP_PF_NONE;
	if (pin == UFL1 && ptp_pins[SMA1].func == func)
		ptp_pins[SMA1].func = PTP_PF_NONE;

	if (pin == SMA2 && ptp_pins[UFL2].func == func)
		ptp_pins[UFL2].func = PTP_PF_NONE;
	if (pin == UFL2 && ptp_pins[SMA2].func == func)
		ptp_pins[SMA2].func = PTP_PF_NONE;

	/* Set up new pin function in the temp table */
	ptp_pins[pin].func = func;

	return ice_ptp_set_e810t_sma_state(hw, ptp_pins);
}

/**
 * ice_e810t_verify_pin
 * @info: the driver's PTP info structure
 * @pin: Pin index
 * @func: Assigned function
 * @chan: Assigned channel
 *
 * Verify if pin supports requested pin function. If the Check pins consistency.
 * Reconfigure the SMA logic attached to the given pin to enable its
 * desired functionality
 */
static int
ice_e810t_verify_pin(struct ptp_clock_info *info, unsigned int pin,
		     enum ptp_pin_function func, unsigned int chan)
{
	/* Don't allow channel reassignment */
	if (chan != ice_e810t_pin_desc[pin].chan)
		return -EOPNOTSUPP;

	/* Check if functions are properly assigned */
	switch (func) {
	case PTP_PF_NONE:
		break;
	case PTP_PF_EXTTS:
		if (pin == UFL1)
			return -EOPNOTSUPP;
		break;
	case PTP_PF_PEROUT:
		if (pin == UFL2 || pin == GNSS)
			return -EOPNOTSUPP;
		break;
	case PTP_PF_PHYSYNC:
		return -EOPNOTSUPP;
	}

	return ice_ptp_set_e810t_sma(info, pin, func);
}



/**
 * mul_u128_u64_fac - Multiplies two 64bit factors to the 128b result
 * @a: First factor to multiply
 * @b: Second factor to multiply
 * @hi: Pointer for higher part of 128b result
 * @lo: Pointer for lower part of 128b result
 *
 * This function performs multiplication of two 64 bit factors with 128b
 * output.
 */
static inline void mul_u128_u64_fac(u64 a, u64 b, u64 *hi, u64 *lo)
{
	u64 mask = GENMASK_ULL(31, 0);
	u64 a_lo = a & mask;
	u64 b_lo = b & mask;

	a >>= 32;
	b >>= 32;

	*hi = (a * b) + (((a * b_lo) + ((a_lo * b_lo) >> 32)) >> 32) +
	      (((a_lo * b) + (((a * b_lo) + ((a_lo * b_lo) >> 32)) & mask)) >> 32);
	*lo = (((a_lo * b) + (((a * b_lo) + ((a_lo * b_lo) >> 32)) & mask)) << 32) +
	      ((a_lo * b_lo) & mask);
}


/**
 * ice_set_tx_tstamp - Enable or disable Tx timestamping
 * @pf: The PF pointer to search in
 * @on: bool value for whether timestamps are enabled or disabled
 */
static void ice_set_tx_tstamp(struct ice_pf *pf, bool on)
{
	struct ice_vsi *vsi;
	u32 val;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;

	vsi->ptp_tx = on;

	/* Enable/disable the TX timestamp interrupt  */
	val = rd32(&pf->hw, PFINT_OICR_ENA);
	if (on)
		val |= PFINT_OICR_TSYN_TX_M;
	else
		val &= ~PFINT_OICR_TSYN_TX_M;
	wr32(&pf->hw, PFINT_OICR_ENA, val);

	if (on)
		pf->ptp.tstamp_config.tx_type = HWTSTAMP_TX_ON;
	else
		pf->ptp.tstamp_config.tx_type = HWTSTAMP_TX_OFF;
}

/**
 * ice_set_rx_tstamp - Enable or disable Rx timestamping
 * @pf: The PF pointer to search in
 * @on: bool value for whether timestamps are enabled or disabled
 */
static void ice_set_rx_tstamp(struct ice_pf *pf, bool on)
{
	struct ice_vsi *vsi;
	u16 i;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;

	ice_for_each_rxq(vsi, i) {
		if (!vsi->rx_rings[i])
			continue;
		vsi->rx_rings[i]->ptp_rx = on;
	}

	if (on)
		pf->ptp.tstamp_config.rx_filter = HWTSTAMP_FILTER_ALL;
	else
		pf->ptp.tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
}


/**
 * ice_ptp_cfg_timestamp - Configure timestamp for init/deinit
 * @pf: Board private structure
 * @ena: bool value to enable or disable time stamp
 *
 * This function will configure timestamping during PTP initialization
 * and deinitialization
 */
static void ice_ptp_cfg_timestamp(struct ice_pf *pf, bool ena)
{
	ice_set_tx_tstamp(pf, ena);
	ice_set_rx_tstamp(pf, ena);

}

/**
 * ice_get_ptp_clock_index - Get the PTP clock index
 * @pf: the PF pointer
 *
 * Determine the clock index of the PTP clock associated with this device. If
 * this is the PF controlling the clock, just use the local access to the
 * clock device pointer.
 *
 * Otherwise, read from the driver shared parameters to determine the clock
 * index value.
 *
 * Returns: the index of the PTP clock associated with this device, or -1 if
 * there is no associated clock.
 */
int ice_get_ptp_clock_index(struct ice_pf *pf)
{
	enum ice_aqc_driver_params param_idx;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u8 tmr_idx;
	u32 value;

	/* Use the ptp_clock structure if we're the main PF */
	if (pf->ptp.clock)
		return ptp_clock_index(pf->ptp.clock);

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_assoc;
	if (!tmr_idx)
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR0;
	else
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR1;

	status = ice_aq_get_driver_param(hw, param_idx, &value, NULL);
	if (status) {
		dev_err(ice_pf_to_dev(pf),
			"Failed to read PTP clock index parameter, err %s aq_err %s\n",
			ice_stat_str(status),
			ice_aq_str(hw->adminq.sq_last_status));
		return -1;
	}

	/* The PTP clock index is an integer, and will be between 0 and
	 * INT_MAX. The highest bit of the driver shared parameter is used to
	 * indicate whether or not the currently stored clock index is valid.
	 */
	if (!(value & PTP_SHARED_CLK_IDX_VALID))
		return -1;

	return value & ~PTP_SHARED_CLK_IDX_VALID;
}

/**
 * ice_set_ptp_clock_index - Set the PTP clock index
 * @pf: the PF pointer
 *
 * Set the PTP clock index for this device into the shared driver parameters,
 * so that other PFs associated with this device can read it.
 *
 * If the PF is unable to store the clock index, it will log an error, but
 * will continue operating PTP.
 */
static void ice_set_ptp_clock_index(struct ice_pf *pf)
{
	enum ice_aqc_driver_params param_idx;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u8 tmr_idx;
	u32 value;

	if (!pf->ptp.clock)
		return;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_assoc;
	if (!tmr_idx)
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR0;
	else
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR1;

	value = (u32)ptp_clock_index(pf->ptp.clock);
	if (value > INT_MAX) {
		dev_err(ice_pf_to_dev(pf), "PTP Clock index is too large to store\n");
		return;
	}
	value |= PTP_SHARED_CLK_IDX_VALID;

	status = ice_aq_set_driver_param(hw, param_idx, value, NULL);
	if (status) {
		dev_err(ice_pf_to_dev(pf),
			"Failed to set PTP clock index parameter, err %s aq_err %s\n",
			ice_stat_str(status),
			ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * ice_clear_ptp_clock_index - Clear the PTP clock index
 * @pf: the PF pointer
 *
 * Clear the PTP clock index for this device. Must be called when
 * unregistering the PTP clock, in order to ensure other PFs stop reporting
 * a clock object that no longer exists.
 */
static void ice_clear_ptp_clock_index(struct ice_pf *pf)
{
	enum ice_aqc_driver_params param_idx;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u8 tmr_idx;

	/* Do not clear the index if we don't own the timer */
	if (!hw->func_caps.ts_func_info.src_tmr_owned)
		return;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_assoc;
	if (!tmr_idx)
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR0;
	else
		param_idx = ICE_AQC_DRIVER_PARAM_CLK_IDX_TMR1;

	status = ice_aq_set_driver_param(hw, param_idx, 0, NULL);
	if (status) {
		dev_dbg(ice_pf_to_dev(pf),
			"Failed to clear PTP clock index parameter, err %s aq_err %s\n",
			ice_stat_str(status),
			ice_aq_str(hw->adminq.sq_last_status));
	}
}

/**
 * ice_ptp_read_src_clk_reg - Read the source clock register
 * @pf: Board private structure
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 */
u64
ice_ptp_read_src_clk_reg(struct ice_pf *pf, struct ptp_system_timestamp *sts)
{
	struct ice_hw *hw = &pf->hw;
	u32 hi, lo, lo2;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	/* Read the system timestamp pre PHC read */
	if (sts)
		ptp_read_system_prets(sts);

	lo = rd32(hw, GLTSYN_TIME_L(tmr_idx));

	/* Read the system timestamp post PHC read */
	if (sts)
		ptp_read_system_postts(sts);

	hi = rd32(hw, GLTSYN_TIME_H(tmr_idx));
	lo2 = rd32(hw, GLTSYN_TIME_L(tmr_idx));

	if (lo2 < lo) {
		/* if TIME_L rolled over read TIME_L again and update
		 *system timestamps
		 */
		if (sts)
			ptp_read_system_prets(sts);
		lo = rd32(hw, GLTSYN_TIME_L(tmr_idx));
		if (sts)
			ptp_read_system_postts(sts);
		hi = rd32(hw, GLTSYN_TIME_H(tmr_idx));
	}

	return ((u64)hi << 32) | lo;
}


/**
 * ice_ptp_update_cached_systime - Update the cached system time values
 * @pf: Board specific private structure
 *
 * This function updates the system time values which are cached in the PF
 * structure and the Rx rings.
 *
 * This should be called periodically at least once a second, and whenever the
 * system time has been adjusted.
 */
static void ice_ptp_update_cached_systime(struct ice_pf *pf)
{
	u64 systime;
	int i;

	/* Read the current system time */
	systime = ice_ptp_read_src_clk_reg(pf, NULL);

	/* Update the cached system time stored in the PF structure */
	WRITE_ONCE(pf->ptp.cached_phc_time, systime);

	ice_for_each_vsi(pf, i) {
		struct ice_vsi *vsi = pf->vsi[i];
		int j;

		if (!vsi)
			continue;

#ifdef HAVE_NETDEV_SB_DEV
		if (vsi->type != ICE_VSI_PF &&
		    vsi->type != ICE_VSI_OFFLOAD_MACVLAN)
			continue;
#else
		if (vsi->type != ICE_VSI_PF)
			continue;
#endif /* HAVE_NETDEV_SB_DEV */

		ice_for_each_rxq(vsi, j) {
			if (!vsi->rx_rings[j])
				continue;
			WRITE_ONCE(vsi->rx_rings[j]->cached_systime, systime);
		}
	}
}

/**
 * ice_ptp_extend_32b_ts - Convert a 32b nanoseconds timestamp to 64b
 * @cached_phc_time: recently cached copy of PHC time
 * @in_tstamp: Ingress/egress 32b nanoseconds timestamp value
 *
 * Hardware captures timestamps which contain only 32 bits of nominal
 * nanoseconds, as opposed to the 64bit timestamps that the stack expects.
 * Note that the captured timestamp values may be 40 bits, but the lower
 * 8 bits are sub-nanoseconds and generally discarded.
 *
 * Extend the 32bit nanosecond timestamp using the following algorithm and
 * assumptions:
 *
 * 1) have a recently cached copy of the PHC time
 * 2) assume that the in_tstamp was captured 2^31 nanoseconds (~2.1
 *    seconds) before or after the PHC time was captured.
 * 3) calculate the delta between the cached time and the timestamp
 * 4) if the delta is smaller than 2^31 nanoseconds, then the timestamp was
 *    captured after the PHC time. In this case, the full timestamp is just
 *    the cached PHC time plus the delta.
 * 5) otherwise, if the delta is larger than 2^31 nanoseconds, then the
 *    timestamp was captured *before* the PHC time, i.e. because the PHC
 *    cache was updated after the timestamp was captured by hardware. In this
 *    case, the full timestamp is the cached time minus the inverse delta.
 *
 * This algorithm works even if the PHC time was updated after a Tx timestamp
 * was requested, but before the Tx timestamp event was reported from
 * hardware.
 *
 * This calculation primarily relies on keeping the cached PHC time up to
 * date. If the timestamp was captured more than 2^31 nanoseconds after the
 * PHC time, it is possible that the lower 32bits of PHC time have
 * overflowed more than once, and we might generate an incorrect timestamp.
 *
 * This is prevented by (a) periodically updating the cached PHC time once
 * a second, and (b) discarding any Tx timestamp packet if it has waited for
 * a timestamp for more than one second.
 */
static u64 ice_ptp_extend_32b_ts(u64 cached_phc_time, u32 in_tstamp)
{
	u32 delta, phc_time_lo;
	u64 ns;

	/* Extract the lower 32 bits of the PHC time */
	phc_time_lo = (u32)cached_phc_time;

	/* Calculate the delta between the lower 32bits of the cached PHC
	 * time and the in_tstamp value
	 */
	delta = (in_tstamp - phc_time_lo);

	/* Do not assume that the in_tstamp is always more recent than the
	 * cached PHC time. If the delta is large, it indicates that the
	 * in_tstamp was taken in the past, and should be converted
	 * forward.
	 */
	if (delta > (U32_MAX / 2)) {
		/* reverse the delta calculation here */
		delta = (phc_time_lo - in_tstamp);
		ns = cached_phc_time - delta;
	} else {
		ns = cached_phc_time + delta;
	}

	return ns;
}

/**
 * ice_ptp_extend_40b_ts - Convert a 40b timestamp to 64b nanoseconds
 * @pf: Board private structure
 * @in_tstamp: Ingress/egress 40b timestamp value
 *
 * The Tx and Rx timestamps are 40 bits wide, including 32 bits of nominal
 * nanoseconds, 7 bits of sub-nanoseconds, and a valid bit.
 *
 *  *--------------------------------------------------------------*
 *  | 32 bits of nanoseconds | 7 high bits of sub ns underflow | v |
 *  *--------------------------------------------------------------*
 *
 * The low bit is an indicator of whether the timestamp is valid. The next
 * 7 bits are a capture of the upper 7 bits of the sub-nanosecond underflow,
 * and the remaining 32 bits are the lower 32 bits of the PHC timer.
 *
 * It is assumed that the caller verifies the timestamp is valid prior to
 * calling this function.
 *
 * Extract the 32bit nominal nanoseconds and extend them. Use the cached PHC
 * time stored in the device private PTP structure as the basis for timestamp
 * extension.
 *
 * See ice_ptp_extend_32b_ts for a detailed explanation of the extension
 * algorithm.
 */
static u64 ice_ptp_extend_40b_ts(struct ice_pf *pf, u64 in_tstamp)
{
	const u64 mask = GENMASK_ULL(31, 0);
	return ice_ptp_extend_32b_ts(pf->ptp.cached_phc_time,
				     (in_tstamp >> 8) & mask);
}

/**
 * ice_ptp_get_ts_idx - Find the free Tx index based on current logical port
 * @vsi: lport corresponding VSI
 */
int ice_ptp_get_ts_idx(struct ice_vsi *vsi)
{
	u8 own_idx_start, own_idx_end, lport, qport;
	int i;

	lport = vsi->port_info->lport;
	qport = lport % ICE_PORTS_PER_QUAD;
	/* Check on own idx window */
	own_idx_start = qport * INDEX_PER_PORT;
	own_idx_end = own_idx_start + INDEX_PER_PORT;

	for (i = own_idx_start; i < own_idx_end; i++) {
		if (!test_and_set_bit(i, vsi->ptp_tx_idx))
			return i;
	}

	return -1;
}

/**
 * ice_ptp_rel_all_skb - Free all pending skb waiting for timestamp
 * @pf: The PF private structure
 */
static void ice_ptp_rel_all_skb(struct ice_pf *pf)
{
	struct ice_vsi *vsi;
	int idx;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;
	for (idx = 0; idx < INDEX_PER_QUAD; idx++) {
		if (vsi->ptp_tx_skb[idx]) {
			dev_kfree_skb_any(vsi->ptp_tx_skb[idx]);
			vsi->ptp_tx_skb[idx] = NULL;
		}
	}
}

static const u64 txrx_lane_par_clk[NUM_ICE_PTP_LNK_SPD] = {
	31250000,	/* 1G */
	257812500,	/* 10G */
	644531250,	/* 25G */
	161132812,	/* 25G RS */
	257812500,	/* 40G */
	644531250,	/* 50G */
	644531250,	/* 50G RS */
	644531250,	/* 100G RS */
};

static const u64 txrx_lane_pcs_clk[NUM_ICE_PTP_LNK_SPD] = {
	125000000,	/* 1G */
	156250000,	/* 10G */
	390625000,	/* 25G */
	97656250,	/* 25G RS */
	156250000,	/* 40G */
	390625000,	/* 50G */
	644531250,	/* 50G RS */
	644531250,	/* 100G RS */
};

static const u64 txrx_rsgb_par_clk[NUM_ICE_PTP_LNK_SPD] = {
	0,		/* 1G */
	0,		/* 10G */
	0,		/* 25G */
	322265625,	/* 25G RS */
	0,		/* 40G */
	0,		/* 50G */
	644531250,	/* 50G RS */
	1289062500,	/* 100G RS */
};

static const u64 txrx_rsgb_pcs_clk[NUM_ICE_PTP_LNK_SPD] = {
	0, 0, 0, 97656250, 0, 0, 195312500, 390625000
};

static const u64 rx_desk_par_pcs_clk[NUM_ICE_PTP_LNK_SPD] = {
	0,		/* 1G */
	0,		/* 10G */
	0,		/* 25G */
	0,		/* 25G RS */
	156250000,	/* 40G */
	19531250,	/* 50G */
	644531250,	/* 50G RS */
	644531250,	/* 100G RS */
};

/**
 * ice_ptp_port_phy_set_parpcs_incval - Set PAR/PCS PHY cycle count
 * @pf: Board private struct
 * @port: Port we are configuring PHY for
 *
 * Note that this function is only expected to be called during port up and
 * during a link event.
 */
static void ice_ptp_port_phy_set_parpcs_incval(struct ice_pf *pf, int port)
{
	u64 cur_freq, clk_incval, uix, phy_tus;
	enum ice_ptp_link_spd link_spd;
	enum ice_ptp_fec_mode fec_mode;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 val;

	cur_freq = ice_e822_pll_freq(pf->ptp.time_ref_freq);
	clk_incval = ice_ptp_read_src_incval(hw);

	status = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, &fec_mode);
	if (status)
		goto exit;

	/* UIX programming */
	/* We split a 'divide by 1e11' operation into a 'divide by 256' and a
	 * 'divide by 390625000' operation to be able to do the calculation
	 * using fixed-point math.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_10G ||
	    link_spd == ICE_PTP_LNK_SPD_40G) {
#define LINE_UI_10G_40G 640 /* 6600 UI at 10Gb line rate */
		uix = (cur_freq * LINE_UI_10G_40G) >> 8;
		uix *= clk_incval;
		uix /= 390625000;

		val = TS_LOW_M & uix;
		status = ice_write_phy_reg_e822(hw, port, P_REG_UIX66_10G_40G_L,
						val);
		if (status)
			goto exit;
		val = (uix >> 32) & TS_LOW_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_UIX66_10G_40G_U,
						val);
		if (status)
			goto exit;
	} else if (link_spd == ICE_PTP_LNK_SPD_25G ||
		   link_spd == ICE_PTP_LNK_SPD_100G_RS) {
#define LINE_UI_25G_100G 256 /* 6600 UI at 25Gb line rate */
		uix = (cur_freq * LINE_UI_25G_100G) >> 8;
		uix *= clk_incval;
		uix /= 390625000;

		val = TS_LOW_M & uix;
		status = ice_write_phy_reg_e822(hw, port,
						P_REG_UIX66_25G_100G_L, val);
		if (status)
			goto exit;
		val = (uix >> 32) & TS_LOW_M;
		status = ice_write_phy_reg_e822(hw, port,
						P_REG_UIX66_25G_100G_U, val);
		if (status)
			goto exit;
	}

	if (link_spd == ICE_PTP_LNK_SPD_25G_RS) {
		phy_tus = (cur_freq * clk_incval * 2) /
			  txrx_rsgb_par_clk[link_spd];
		val = phy_tus & TS_PHY_LOW_M;
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PAR_RX_TUS_L, val);
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PAR_TX_TUS_L, val);
		val = (phy_tus >> 8) & TS_PHY_HIGH_M;
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PAR_RX_TUS_U, val);
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PAR_TX_TUS_U, val);

		phy_tus = (cur_freq * clk_incval) /
			  txrx_rsgb_pcs_clk[link_spd];
		val = phy_tus & TS_PHY_LOW_M;
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PCS_RX_TUS_L, val);
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PCS_TX_TUS_L, val);
		val = (phy_tus >> 8) & TS_PHY_HIGH_M;
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PCS_RX_TUS_U, val);
		ice_write_phy_reg_e822(hw, port, P_REG_DESK_PCS_TX_TUS_U, val);
	} else {
		phy_tus = (cur_freq * clk_incval) /
			txrx_lane_par_clk[link_spd];
		val = phy_tus & TS_PHY_LOW_M;
		ice_write_phy_reg_e822(hw, port, P_REG_PAR_RX_TUS_L, val);
		val = (phy_tus >> 8) & TS_PHY_HIGH_M;
		ice_write_phy_reg_e822(hw, port, P_REG_PAR_RX_TUS_U, val);

		if (link_spd != ICE_PTP_LNK_SPD_50G_RS &&
		    link_spd != ICE_PTP_LNK_SPD_100G_RS) {
			val = phy_tus & TS_PHY_LOW_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_PAR_TX_TUS_L, val);
			val = (phy_tus >> 8) & TS_PHY_HIGH_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_PAR_TX_TUS_U, val);
		} else {
			phy_tus = (cur_freq * clk_incval * 2) /
				txrx_rsgb_par_clk[link_spd];
			val = phy_tus & TS_PHY_LOW_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_RX_TUS_L, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_TX_TUS_L, val);
			val = (phy_tus >> 8) & TS_PHY_HIGH_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_RX_TUS_U, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_TX_TUS_U, val);
		}

		phy_tus = (cur_freq * clk_incval) /
			txrx_lane_pcs_clk[link_spd];
		val = phy_tus & TS_PHY_LOW_M;
		ice_write_phy_reg_e822(hw, port, P_REG_PCS_RX_TUS_L, val);
		val = (phy_tus >> 8) & TS_PHY_HIGH_M;
		ice_write_phy_reg_e822(hw, port, P_REG_PCS_RX_TUS_U, val);

		if (link_spd != ICE_PTP_LNK_SPD_50G_RS &&
		    link_spd != ICE_PTP_LNK_SPD_100G_RS) {
			val = phy_tus & TS_PHY_LOW_M;
			ice_write_phy_reg_e822(hw, port, P_REG_PCS_TX_TUS_L,
					       val);
			val = (phy_tus >> 8) & TS_PHY_HIGH_M;
			ice_write_phy_reg_e822(hw, port, P_REG_PCS_TX_TUS_U,
					       val);
		} else {
			phy_tus = (cur_freq * clk_incval) /
				txrx_rsgb_pcs_clk[link_spd];
			val = phy_tus & TS_PHY_LOW_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_RX_TUS_L, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_TX_TUS_L, val);
			val = (phy_tus >> 8) & TS_PHY_HIGH_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_RX_TUS_U, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_TX_TUS_U, val);
		}

		if (link_spd == ICE_PTP_LNK_SPD_40G ||
		    link_spd == ICE_PTP_LNK_SPD_50G) {
			phy_tus = (cur_freq * clk_incval) /
				rx_desk_par_pcs_clk[link_spd];
			val = phy_tus & TS_PHY_LOW_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_RX_TUS_L, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_RX_TUS_L, val);
			val = (phy_tus >> 8) & TS_PHY_HIGH_M;
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PAR_RX_TUS_U, val);
			ice_write_phy_reg_e822(hw, port,
					       P_REG_DESK_PCS_RX_TUS_U, val);
		}
	}

exit:
	if (status)
		dev_err(ice_pf_to_dev(pf), "PTP Vernier configuration failed on port %d, status %s\n",
			port, ice_stat_str(status));
}

/* Values of tx_offset_delay in units of 1/100th of a nanosecond */
static const u64 tx_offset_delay[NUM_ICE_PTP_LNK_SPD] = {
	25140,	/* 1G */
	6938,	/* 10G */
	2778,	/* 25G */
	3928,	/* 25G RS */
	5666,	/* 40G */
	2778,	/* 50G */
	2095,	/* 50G RS */
	1620,	/* 100G RS */
};

/**
 * ice_ptp_port_phy_set_tx_offset - Set PHY clock Tx timestamp offset
 * @ptp_port: the PTP port we are configuring the PHY for
 */
static int ice_ptp_port_phy_set_tx_offset(struct ice_ptp_port *ptp_port)
{
	u64 cur_freq, clk_incval, offset;
	enum ice_ptp_link_spd link_spd;
	enum ice_status status;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int port;
	u32 val;

	pf = ptp_port_to_pf(ptp_port);
	port = ptp_port->port_num;
	hw = &pf->hw;

	/* Get the PTP HW lock */
	if (!ice_ptp_lock(hw))
		return -EBUSY;

	clk_incval = ice_ptp_read_src_incval(hw);
	ice_ptp_unlock(hw);

	cur_freq = ice_e822_pll_freq(pf->ptp.time_ref_freq);

	status = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, NULL);
	if (status)
		goto exit;

	offset = cur_freq * clk_incval;
	offset /= 10000;
	offset *= tx_offset_delay[link_spd];
	offset /= 10000000;

	if (link_spd == ICE_PTP_LNK_SPD_1G ||
	    link_spd == ICE_PTP_LNK_SPD_10G ||
	    link_spd == ICE_PTP_LNK_SPD_25G ||
	    link_spd == ICE_PTP_LNK_SPD_25G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_40G ||
	    link_spd == ICE_PTP_LNK_SPD_50G) {
		status = ice_read_phy_reg_e822(hw, port,
					       P_REG_PAR_PCS_TX_OFFSET_L,
					       &val);
		if (status)
			goto exit;
		offset += val;
		status = ice_read_phy_reg_e822(hw, port,
					       P_REG_PAR_PCS_TX_OFFSET_U,
					       &val);
		if (status)
			goto exit;
		offset += (u64)val << 32;
	}

	if (link_spd == ICE_PTP_LNK_SPD_50G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_TX_TIME_L,
					       &val);
		if (status)
			goto exit;
		offset += val;
		status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_TX_TIME_U,
					       &val);
		if (status)
			goto exit;
		offset += (u64)val << 32;
	}

	val = (u32)offset;
	status = ice_write_phy_reg_e822(hw, port, P_REG_TOTAL_TX_OFFSET_L, val);
	if (status)
		goto exit;
	val = (u32)(offset >> 32);
	status = ice_write_phy_reg_e822(hw, port, P_REG_TOTAL_TX_OFFSET_U, val);
	if (status)
		goto exit;

	status = ice_write_phy_reg_e822(hw, port, P_REG_TX_OR, 1);
	if (status)
		goto exit;

	atomic_set(&ptp_port->tx_offset_ready, 1);
exit:
	if (status)
		dev_err(ice_pf_to_dev(pf),
			"PTP tx offset configuration failed on port %d status=%s\n",
			port, ice_stat_str(status));
	return ice_status_to_errno(status);
}

/**
 * ice_ptp_calc_pmd_adj - Calculate PMD adjustment using integers
 * @cur_freq: PHY clock frequency
 * @clk_incval: Source clock incval
 * @calc_numerator: Value to divide
 * @calc_denominator: Remainder of the division
 *
 * This is the integer math calculation which attempts to avoid overflowing
 * a u64. The division (in this case 1/25.78125e9) is split into two parts 125
 * and the remainder, which is the stored in calc_denominator.
 */
static u64
ice_ptp_calc_pmd_adj(u64 cur_freq, u64 clk_incval, u64 calc_numerator,
		     u64 calc_denominator)
{
	u64 pmd_adj = calc_numerator;

	pmd_adj *= cur_freq;
	pmd_adj /= 125;
	pmd_adj *= clk_incval;
	pmd_adj /= calc_denominator;
	return pmd_adj;
}

/**
 * ice_ptp_get_pmd_adj - Calculate total PMD adjustment
 * @pf: Board private struct
 * @port: Port we are configuring PHY for
 * @cur_freq: PHY clock frequency
 * @link_spd: PHY link speed
 * @clk_incval: source clock incval
 * @mode: FEC mode
 * @pmd_adj: PMD adjustment to be calculated
 */
static int ice_ptp_get_pmd_adj(struct ice_pf *pf, int port, u64 cur_freq,
			       enum ice_ptp_link_spd link_spd, u64 clk_incval,
			       enum ice_ptp_fec_mode mode, u64 *pmd_adj)
{
	u64 calc_numerator, calc_denominator;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 val;
	u8 pmd;

	status = ice_read_phy_reg_e822(hw, port, P_REG_PMD_ALIGNMENT, &val);
	if (status)
		return -EIO;

	pmd = (u8)val;

	/* RS mode overrides all the other pmd_alignment calculations. */
	if (link_spd == ICE_PTP_LNK_SPD_25G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_50G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		u64 pmd_cycle_adj = 0;
		u8 rx_cycle;

		if (link_spd == ICE_PTP_LNK_SPD_50G ||
		    link_spd == ICE_PTP_LNK_SPD_50G_RS) {
			ice_read_phy_reg_e822(hw, port, P_REG_RX_80_TO_160_CNT,
					      &val);
			rx_cycle = val & P_REG_RX_80_TO_160_CNT_RXCYC_M;
		} else {
			ice_read_phy_reg_e822(hw, port, P_REG_RX_40_TO_160_CNT,
					      &val);
			rx_cycle = val & P_REG_RX_40_TO_160_CNT_RXCYC_M;
		}
		calc_numerator = pmd;
		if (pmd < 17)
			calc_numerator += 40;
		calc_denominator = 206250000;

		*pmd_adj = ice_ptp_calc_pmd_adj(cur_freq, clk_incval,
						calc_numerator,
						calc_denominator);

		if (rx_cycle != 0) {
			if (link_spd == ICE_PTP_LNK_SPD_25G_RS)
				calc_numerator = 4 - rx_cycle;
			else if (link_spd == ICE_PTP_LNK_SPD_50G_RS)
				calc_numerator = rx_cycle;
			else
				calc_numerator = 0;
			calc_numerator *= 40;
			pmd_cycle_adj = ice_ptp_calc_pmd_adj(cur_freq,
							     clk_incval,
							     calc_numerator,
							     calc_denominator);
		}
		*pmd_adj += pmd_cycle_adj;
	} else {
		calc_numerator = 0;
		calc_denominator = 1;
		if (link_spd == ICE_PTP_LNK_SPD_1G) {
			if (pmd == 4)
				calc_numerator = 10;
			else
				calc_numerator = (pmd + 6) % 10;
			calc_denominator = 10000000;
		} else if (link_spd == ICE_PTP_LNK_SPD_10G ||
			   link_spd == ICE_PTP_LNK_SPD_40G) {
			if (pmd != 65 || mode == ICE_PTP_FEC_MODE_CLAUSE74) {
				calc_numerator = pmd;
				calc_denominator = 82500000;
			}
		} else if (link_spd == ICE_PTP_LNK_SPD_25G) {
			if (pmd != 65 || mode == ICE_PTP_FEC_MODE_CLAUSE74) {
				calc_numerator = pmd;
				calc_denominator = 206250000;
			}
		} else if (link_spd == ICE_PTP_LNK_SPD_50G) {
			if (pmd != 65 || mode == ICE_PTP_FEC_MODE_CLAUSE74) {
				calc_numerator = pmd * 2;
				calc_denominator = 206250000;
			}
		}
		*pmd_adj = ice_ptp_calc_pmd_adj(cur_freq, clk_incval,
						calc_numerator,
						calc_denominator);
	}

	return 0;
}

/* Values of rx_offset_delay in units of 1/100th of a nanosecond */
static const u64 rx_offset_delay[NUM_ICE_PTP_LNK_SPD] = {
	17372,	/* 1G */
	6212,	/* 10G */
	2491,	/* 25G */
	29535,	/* 25G RS */
	4244,	/* 40G */
	2868,	/* 50G */
	14524,	/* 50G RS */
	7775,	/* 100G RS */
};

/**
 * ice_ptp_port_phy_set_rx_offset - Set PHY clock Tx timestamp offset
 * @ptp_port: PTP port we are configuring PHY for
 */
static int ice_ptp_port_phy_set_rx_offset(struct ice_ptp_port *ptp_port)
{
	u64 cur_freq, clk_incval, offset, pmd_adj;
	enum ice_ptp_link_spd link_spd;
	enum ice_ptp_fec_mode fec_mode;
	enum ice_status status;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int err, port;
	u32 val;

	pf = ptp_port_to_pf(ptp_port);
	port = ptp_port->port_num;
	hw = &pf->hw;

	/* Get the PTP HW lock */
	if (!ice_ptp_lock(hw)) {
		err = -EBUSY;
		goto exit;
	}

	clk_incval = ice_ptp_read_src_incval(hw);
	ice_ptp_unlock(hw);

	cur_freq = ice_e822_pll_freq(pf->ptp.time_ref_freq);

	status = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, &fec_mode);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}

	offset = cur_freq * clk_incval;
	offset /= 10000;
	offset *= rx_offset_delay[link_spd];
	offset /= 10000000;

	status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_PCS_RX_OFFSET_L,
				       &val);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}
	offset += val;
	status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_PCS_RX_OFFSET_U,
				       &val);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}
	offset += (u64)val << 32;

	if (link_spd == ICE_PTP_LNK_SPD_40G ||
	    link_spd == ICE_PTP_LNK_SPD_50G ||
	    link_spd == ICE_PTP_LNK_SPD_50G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_RX_TIME_L,
					       &val);
		if (status) {
			err = ice_status_to_errno(status);
			goto exit;
		}
		offset += val;
		status = ice_read_phy_reg_e822(hw, port, P_REG_PAR_RX_TIME_U,
					       &val);
		if (status) {
			err = ice_status_to_errno(status);
			goto exit;
		}
		offset += (u64)val << 32;
	}

	err = ice_ptp_get_pmd_adj(pf, port, cur_freq, link_spd, clk_incval,
				  fec_mode, &pmd_adj);
	if (err)
		goto exit;

	if (fec_mode == ICE_PTP_FEC_MODE_RS_FEC)
		offset += pmd_adj;
	else
		offset -= pmd_adj;

	val = (u32)offset;
	status = ice_write_phy_reg_e822(hw, port, P_REG_TOTAL_RX_OFFSET_L, val);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}
	val = (u32)(offset >> 32);
	status = ice_write_phy_reg_e822(hw, port, P_REG_TOTAL_RX_OFFSET_U, val);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}

	status = ice_write_phy_reg_e822(hw, port, P_REG_RX_OR, 1);
	if (status) {
		err = ice_status_to_errno(status);
		goto exit;
	}

	atomic_set(&ptp_port->rx_offset_ready, 1);
exit:
	if (err)
		dev_err(ice_pf_to_dev(pf),
			"PTP rx offset configuration failed on port %d, err=%d\n",
			port, err);
	return err;
}

/**
 * ice_ptp_port_sync_src_timer - Sync PHY timer with source timer
 * @pf: Board private structure
 * @port: Port for which the PHY start is set
 *
 * Sync PHY timer with source timer after calculating and setting Tx/Rx
 * Vernier offset.
 */
static enum ice_status ice_ptp_port_sync_src_timer(struct ice_pf *pf, int port)
{
	u64 src_time = 0x0, tx_time, rx_time, temp_adj;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	s64 time_adj;
	u32 zo, lo;
	u8 tmr_idx;

	/* Get the PTP HW lock */
	if (!ice_ptp_lock(hw)) {
		dev_err(dev, "PTP failed to acquire semaphore\n");
		return ICE_ERR_NOT_READY;
	}

	/* Program cmd to source timer */
	ice_ptp_src_cmd(hw, READ_TIME);

	/* Program cmd to PHY port */
	status = ice_ptp_one_port_cmd(hw, port, READ_TIME, true);
	if (status)
		goto unlock;

	/* Issue sync to activate commands */
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);

	tmr_idx = ice_get_ptp_src_clock_index(hw);

	/* Read source timer SHTIME_0 and SHTIME_L */
	zo = rd32(hw, GLTSYN_SHTIME_0(tmr_idx));
	lo = rd32(hw, GLTSYN_SHTIME_L(tmr_idx));
	src_time |= (u64)lo;
	src_time = (src_time << 32) | (u64)zo;

	/* Read Tx and Rx capture from PHY */
	status = ice_ptp_read_port_capture(hw, port, &tx_time, &rx_time);
	if (status)
		goto unlock;

	if (tx_time != rx_time)
		dev_info(dev, "Port %d Rx and Tx times do not match\n", port);

	/* Calculate amount to adjust port timer and account for case where
	 * delta is larger/smaller than S64_MAX/S64_MIN
	 */
	if (src_time > tx_time) {
		temp_adj = src_time - tx_time;
		if (temp_adj & BIT_ULL(63)) {
			time_adj = temp_adj >> 1;
		} else {
			time_adj = temp_adj;
			/* Set to zero to indicate adjustment done */
			temp_adj = 0x0;
		}
	} else {
		temp_adj = tx_time - src_time;
		if (temp_adj & BIT_ULL(63)) {
			time_adj = -(temp_adj >> 1);
		} else {
			time_adj = -temp_adj;
			/* Set to zero to indicate adjustment done */
			temp_adj = 0x0;
		}
	}

	status = ice_ptp_prep_port_adj_e822(hw, port, time_adj, true);
	if (status)
		goto unlock;

	status = ice_ptp_one_port_cmd(hw, port, ADJ_TIME, true);
	if (status)
		goto unlock;

	/* Issue sync to activate commands */
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);

	/* Do a second adjustment if original was too large/small to fit into
	 * a S64
	 */
	if (temp_adj) {
		status = ice_ptp_prep_port_adj_e822(hw, port, time_adj, true);
		if (status)
			goto unlock;

		status = ice_ptp_one_port_cmd(hw, port, ADJ_TIME, true);
		if (!status)
			/* Issue sync to activate commands */
			wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);
	}

	/* This second register read is to flush out the port and source
	 * command registers. Multiple successive calls to this function
	 * require this
	 */

	/* Program cmd to source timer */
	ice_ptp_src_cmd(hw, READ_TIME);

	/* Program cmd to PHY port */
	status = ice_ptp_one_port_cmd(hw, port, READ_TIME, true);
	if (status)
		goto unlock;

	/* Issue sync to activate commands */
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);

	/* Read source timer SHTIME_0 and SHTIME_L */
	zo = rd32(hw, GLTSYN_SHTIME_0(tmr_idx));
	lo = rd32(hw, GLTSYN_SHTIME_L(tmr_idx));
	src_time = (u64)lo;
	src_time = (src_time << 32) | (u64)zo;

	/* Read Tx and Rx capture from PHY */
	status = ice_ptp_read_port_capture(hw, port, &tx_time, &rx_time);

	if (status)
		goto unlock;
	dev_info(dev, "Port %d PTP synced to source 0x%016llX, 0x%016llX\n",
		 port, src_time, tx_time);
unlock:
	ice_ptp_unlock(hw);

	if (status)
		dev_err(dev, "PTP failed to sync port %d PHY time, status %s\n",
			port, ice_stat_str(status));

	return status;
}

/**
 * ice_ptp_read_time - Read the time from the device
 * @pf: Board private structure
 * @ts: timespec structure to hold the current time value
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 *
 * This function reads the source clock registers and stores them in a timespec.
 * However, since the registers are 64 bits of nanoseconds, we must convert the
 * result to a timespec before we can return.
 */
static void ice_ptp_read_time(struct ice_pf *pf, struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	u64 time_ns;

	if (pf->ptp.src_tmr_mode != ICE_SRC_TMR_MODE_NANOSECONDS) {
		dev_err(ice_pf_to_dev(pf),
			"PTP Locked mode is not supported!\n");
		return;
	}
	time_ns = ice_ptp_read_src_clk_reg(pf, sts);

	*ts = ns_to_timespec64(time_ns);
}

/**
 * ice_ptp_write_init - Set PHC time to provided value
 * @pf: Board private structure
 * @ts: timespec structure that holds the new time value
 *
 * Set the PHC time to the specified time provided in the timespec.
 */
static int ice_ptp_write_init(struct ice_pf *pf, struct timespec64 *ts)
{
	u64 ns = timespec64_to_ns(ts);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u64 val;

	if (pf->ptp.src_tmr_mode != ICE_SRC_TMR_MODE_NANOSECONDS) {
		dev_err(ice_pf_to_dev(pf),
			"PTP Locked mode is not supported!\n");
		return ICE_ERR_NOT_SUPPORTED;
	}
	val = ns;

	status = ice_ptp_init_time(hw, val);
	if (status)
		return ice_status_to_errno(status);

	return 0;
}

/**
 * ice_ptp_write_adj - Adjust PHC clock time atomically
 * @pf: Board private structure
 * @adj: Adjustment in nanoseconds
 * @lock_sbq: true to lock the sbq sq_lock (the usual case); false if the
 *            sq_lock has already been locked at a higher level
 *
 * Perform an atomic adjustment of the PHC time by the specified number of
 * nanoseconds.
 */
static int
ice_ptp_write_adj(struct ice_pf *pf, s32 adj, bool lock_sbq)
{
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;


	status = ice_ptp_adj_clock(hw, adj, lock_sbq);
	if (status)
		return ice_status_to_errno(status);

	return 0;
}


/**
 * ice_ptp_get_incval - Get clock increment params
 * @pf: Board private structure
 * @time_ref_freq: TIME_REF frequency
 * @src_tmr_mode: Source timer mode (nanoseconds or locked)
 */
int ice_ptp_get_incval(struct ice_pf *pf, enum ice_time_ref_freq *time_ref_freq,
		       enum ice_src_tmr_mode *src_tmr_mode)
{
	*time_ref_freq = pf->ptp.time_ref_freq;
	*src_tmr_mode = pf->ptp.src_tmr_mode;

	return 0;
}

/**
 * ice_base_incval - Get base timer increment value
 * @pf: Board private structure
 *
 * Look up the base timer increment value for this device. The base increment
 * value is used to define the nominal clock tick rate. This increment value
 * is programmed during device initialization. It is also used as the basis
 * for calculating adjustments using scaled_ppm.
 */
static u64 ice_base_incval(struct ice_pf *pf)
{
	u64 incval;

	if (ice_is_e810(&pf->hw))
		incval = ICE_PTP_NOMINAL_INCVAL_E810;
	else if (pf->ptp.time_ref_freq < NUM_ICE_TIME_REF_FREQ)
		incval = ice_e822_nominal_incval(pf->ptp.time_ref_freq);
	else
		incval = LOCKED_INCVAL_E822;

	dev_dbg(ice_pf_to_dev(pf), "PTP: using base increment value of 0x%016llx\n",
		incval);

	return incval;
}

/**
 * ice_ptp_reset_ts_memory_quad - Reset timestamp memory for one quad
 * @pf: The PF private data structure
 * @quad: The quad (0-4)
 */
static void ice_ptp_reset_ts_memory_quad(struct ice_pf *pf, int quad)
{
	struct ice_hw *hw = &pf->hw;

	ice_write_quad_reg_e822(hw, quad, Q_REG_TS_CTRL, Q_REG_TS_CTRL_M);
	ice_write_quad_reg_e822(hw, quad, Q_REG_TS_CTRL, ~(u32)Q_REG_TS_CTRL_M);
}

/**
 * ice_ptp_check_tx_fifo - Check whether Tx FIFO is in an OK state
 * @port: PTP port for which Tx FIFO is checked
 */
static int ice_ptp_check_tx_fifo(struct ice_ptp_port *port)
{
	int quad = port->port_num / ICE_PORTS_PER_QUAD;
	int offs = port->port_num % ICE_PORTS_PER_QUAD;
	enum ice_status status;
	struct ice_pf *pf;
	struct ice_hw *hw;
	u32 val, phy_sts;

	pf = ptp_port_to_pf(port);
	hw = &pf->hw;


	if (port->tx_fifo_busy_cnt == FIFO_OK)
		return 0;

	/* need to read FIFO state */
	if (offs == 0 || offs == 1)
		status = ice_read_quad_reg_e822(hw, quad, Q_REG_FIFO01_STATUS,
						&val);
	else
		status = ice_read_quad_reg_e822(hw, quad, Q_REG_FIFO23_STATUS,
						&val);

	if (status) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to check port %d Tx FIFO, status %s\n",
			port->port_num, ice_stat_str(status));
		return ice_status_to_errno(status);
	}

	if (offs & 0x1)
		phy_sts = (val & Q_REG_FIFO13_M) >> Q_REG_FIFO13_S;
	else
		phy_sts = (val & Q_REG_FIFO02_M) >> Q_REG_FIFO02_S;

	if (phy_sts & FIFO_EMPTY) {
		port->tx_fifo_busy_cnt = FIFO_OK;
		return 0;
	}

	port->tx_fifo_busy_cnt++;

	dev_dbg(ice_pf_to_dev(pf), "Try %d, port %d FIFO not empty\n",
		port->tx_fifo_busy_cnt, port->port_num);

	if (port->tx_fifo_busy_cnt == ICE_PTP_FIFO_NUM_CHECKS) {
		dev_dbg(ice_pf_to_dev(pf),
			"Port %d Tx FIFO still not empty; resetting quad %d\n",
			port->port_num, quad);
		ice_ptp_reset_ts_memory_quad(pf, quad);
		port->tx_fifo_busy_cnt = FIFO_OK;
		return 0;
	}

	return -EAGAIN;
}

/**
 * ice_ptp_check_tx_offset_valid - Check if the Tx PHY offset is valid
 * @port: the PTP port to check
 *
 * Checks whether the Tx offset for the PHY associated with this port is
 * valid. Returns 0 if the offset is valid, and a non-zero error code if it is
 * not.
 */
static int ice_ptp_check_tx_offset_valid(struct ice_ptp_port *port)
{
	struct ice_pf *pf = ptp_port_to_pf(port);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 val;
	int err;

	/* Check if the offset is already valid */
	if (atomic_read(&port->tx_offset_ready))
		return 0;

	/* Take the bit lock to prevent cross thread interaction */
	if (atomic_cmpxchg(&port->tx_offset_lock, false, true))
		return -EBUSY;

	err = ice_ptp_check_tx_fifo(port);
	if (err)
		goto out_unlock;

	status = ice_read_phy_reg_e822(hw, port->port_num, P_REG_TX_OV_STATUS,
				       &val);
	if (status) {
		dev_err(dev, "Failed to read TX_OV_STATUS for port %d, status %s\n",
			port->port_num, ice_stat_str(status));
		err = -EAGAIN;
		goto out_unlock;
	}

	if (!(val & P_REG_TX_OV_STATUS_OV_M)) {
		err = -EAGAIN;
		goto out_unlock;
	}

	err = ice_ptp_port_phy_set_tx_offset(port);
	if (err) {
		dev_err(dev, "Failed to set PHY Rx offset for port %d, err %d\n",
			port->port_num, err);
		goto out_unlock;
	}

	dev_info(dev, "Port %d Tx calibration complete\n", port->port_num);


out_unlock:
	atomic_set(&port->tx_offset_lock, false);

	return err;
}

/**
 * ice_ptp_check_rx_offset_valid - Check if the Rx PHY offset is valid
 * @port: the PTP port to check
 *
 * Checks whether the Rx offset for the PHY associated with this port is
 * valid. Returns 0 if the offset is valid, and a non-zero error code if it is
 * not.
 */
static int ice_ptp_check_rx_offset_valid(struct ice_ptp_port *port)
{
	struct ice_pf *pf = ptp_port_to_pf(port);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 val;
	int err;

	/* Check if the offset is already valid */
	if (atomic_read(&port->rx_offset_ready))
		return 0;

	/* Take the bit lock to prevent cross thread interaction */
	if (atomic_cmpxchg(&port->rx_offset_lock, false, true))
		return -EBUSY;

	status = ice_read_phy_reg_e822(hw, port->port_num, P_REG_RX_OV_STATUS,
				       &val);
	if (status) {
		dev_err(dev, "Failed to read RX_OV_STATUS for port %d, status %s\n",
			port->port_num, ice_stat_str(status));
		err = ice_status_to_errno(status);
		goto out_unlock;
	}

	if (!(val & P_REG_RX_OV_STATUS_OV_M)) {
		err = -EAGAIN;
		goto out_unlock;
	}

	err = ice_ptp_port_phy_set_rx_offset(port);
	if (err) {
		dev_err(dev, "Failed to set PHY Rx offset for port %d, err %d\n",
			port->port_num, err);
		goto out_unlock;
	}

	dev_info(dev, "Port %d Rx calibration complete\n", port->port_num);

out_unlock:
	atomic_set(&port->rx_offset_lock, false);

	return err;
}

/**
 * ice_ptp_check_offset_valid - Check port offset valid bit
 * @port: Port for which offset valid bit is checked
 *
 * Returns 0 if both Tx and Rx offset are valid, and -EAGAIN if one of the
 * offset is not ready.
 */
static int ice_ptp_check_offset_valid(struct ice_ptp_port *port)
{
	int tx_err, rx_err;

	/* always check both Tx and Rx offset validity */
	tx_err = ice_ptp_check_tx_offset_valid(port);
	rx_err = ice_ptp_check_rx_offset_valid(port);

	if (tx_err || rx_err)
		return -EAGAIN;

	return 0;
}

/**
 * ice_ptp_wait_for_offset_valid - Poll offset valid reg until set or timeout
 * @work: Pointer to struct work_struct
 */
static void ice_ptp_wait_for_offset_valid(struct work_struct *work)
{
	struct ice_ptp_port *port;
	struct ice_pf *pf;
	int i;

	port = container_of(work, struct ice_ptp_port, ov_task);
	pf = ptp_port_to_pf(port);

#define OV_POLL_PERIOD_MS 10
#define OV_POLL_ATTEMPTS 20
	for (i = 0; i < OV_POLL_ATTEMPTS; i++) {
		if (atomic_read(&pf->ptp.phy_reset_lock))
			return;

		if (!ice_ptp_check_offset_valid(port))
			return;

		msleep(OV_POLL_PERIOD_MS);
	}
}

/**
 * ice_ptp_port_phy_start - Set or clear PHY start for port timestamping
 * @ptp_port: PTP port for which the PHY start is set
 * @phy_start: Value to be set
 */
static int
ice_ptp_port_phy_start(struct ice_ptp_port *ptp_port, bool phy_start)
{
	struct ice_pf *pf = ptp_port_to_pf(ptp_port);
	u8 port = ptp_port->port_num;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 val;

	mutex_lock(&ptp_port->ps_lock);

	atomic_set(&ptp_port->tx_offset_ready, 0);
	atomic_set(&ptp_port->rx_offset_ready, 0);
	ptp_port->tx_fifo_busy_cnt = 0;

	status = ice_write_phy_reg_e822(hw, port, P_REG_TX_OR, 0);
	if (status)
		goto out_unlock;

	status = ice_write_phy_reg_e822(hw, port, P_REG_RX_OR, 0);
	if (status)
		goto out_unlock;

	status = ice_read_phy_reg_e822(hw, port, P_REG_PS, &val);
	if (status)
		goto out_unlock;

	val &= ~P_REG_PS_START_M;
	status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (status)
		goto out_unlock;

	val &= ~P_REG_PS_ENA_CLK_M;
	status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (status)
		goto out_unlock;


	if (phy_start && ptp_port->link_up) {
		ice_phy_cfg_lane_e822(hw, port);
		ice_ptp_port_phy_set_parpcs_incval(pf, port);

		status = ice_ptp_write_incval_locked(hw, ice_base_incval(pf));
		if (status)
			goto out_unlock;


		status = ice_read_phy_reg_e822(hw, port, P_REG_PS, &val);
		if (status)
			goto out_unlock;

		val |= P_REG_PS_SFT_RESET_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (status)
			goto out_unlock;

		val |= P_REG_PS_START_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (status)
			goto out_unlock;

		val &= ~P_REG_PS_SFT_RESET_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (status)
			goto out_unlock;

		status = ice_ptp_write_incval_locked(hw, ice_base_incval(pf));
		if (status)
			goto out_unlock;

		val |= P_REG_PS_ENA_CLK_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (status)
			goto out_unlock;

		val |= P_REG_PS_LOAD_OFFSET_M;
		status = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (status)
			goto out_unlock;

		wr32(&pf->hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);
		status = ice_ptp_port_sync_src_timer(pf, port);
		if (status)
			goto out_unlock;

		queue_work(pf->ptp.ov_wq, &ptp_port->ov_task);
	}

out_unlock:
	if (status)
		dev_err(ice_pf_to_dev(pf), "PTP failed to set PHY port %d %s, status=%s\n",
			port, phy_start ? "up" : "down", ice_stat_str(status));

	mutex_unlock(&ptp_port->ps_lock);

	return ice_status_to_errno(status);
}

/**
 * ice_ptp_link_change - Set or clear port registers for timestamping
 * @pf: Board private structure
 * @port: Port for which the PHY start is set
 * @linkup: Link is up or down
 */
int ice_ptp_link_change(struct ice_pf *pf, u8 port, bool linkup)
{
	/* If PTP is not supported on this function, nothing to do */
	if (!test_bit(ICE_FLAG_PTP_ENA, pf->flags))
		return 0;

	if (linkup && !test_bit(ICE_FLAG_PTP, pf->flags)) {
		dev_err(ice_pf_to_dev(pf), "PTP not ready, failed to prepare port %d\n",
			port);
		return -EAGAIN;
	}

	if (port >= ICE_NUM_EXTERNAL_PORTS)
		return -EINVAL;

	pf->ptp.port.link_up = linkup;

	return ice_ptp_port_phy_start(&pf->ptp.port, linkup);
}


/**
 * ice_ptp_reset_ts_memory - Reset timestamp memory for all quads
 * @pf: The PF private data structure
 */
static void ice_ptp_reset_ts_memory(struct ice_pf *pf)
{
	int quad;

	quad = pf->hw.port_info->lport / ICE_PORTS_PER_QUAD;
	ice_ptp_reset_ts_memory_quad(pf, quad);
}

/**
 * ice_ptp_tx_ena_intr - Enable or disable the Tx timestamp interrupt
 * @pf: PF private structure
 * @ena: bool value to enable or disable interrupt
 * @threshold: Minimum number of packets at which intr is triggered
 *
 * Utility function to enable or disable Tx timestamp interrupt and threshold
 */
static int ice_ptp_tx_ena_intr(struct ice_pf *pf, bool ena, u32 threshold)
{
	enum ice_status status = 0;
	struct ice_hw *hw = &pf->hw;
	int quad;
	u32 val;

	ice_ptp_reset_ts_memory(pf);

	for (quad = 0; quad < ICE_MAX_QUAD; quad++) {
		status = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG,
						&val);
		if (status)
			break;

		if (ena) {
			val |= Q_REG_TX_MEM_GBL_CFG_INTR_ENA_M;
			val &= ~Q_REG_TX_MEM_GBL_CFG_INTR_THR_M;
			val |= ((threshold << Q_REG_TX_MEM_GBL_CFG_INTR_THR_S) &
				Q_REG_TX_MEM_GBL_CFG_INTR_THR_M);
		} else {
			val &= ~Q_REG_TX_MEM_GBL_CFG_INTR_ENA_M;
		}

		status = ice_write_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG,
						 val);
		if (status)
			break;
	}

	if (status)
		dev_err(ice_pf_to_dev(pf), "PTP failed in intr ena, status %s\n",
			ice_stat_str(status));
	return ice_status_to_errno(status);
}

/**
 * ice_ptp_reset_phy_timestamping - Reset PHY timestamp registers values
 * @pf: Board private structure
 */
static void ice_ptp_reset_phy_timestamping(struct ice_pf *pf)
{
	int i;

#define PHY_RESET_TRIES		5
#define PHY_RESET_SLEEP_MS	5

	for (i = 0; i < PHY_RESET_TRIES; i++) {
		if (atomic_cmpxchg(&pf->ptp.phy_reset_lock, false, true))
			goto reset;

		msleep(PHY_RESET_SLEEP_MS);
	}
	return;

reset:
	flush_workqueue(pf->ptp.ov_wq);
	ice_ptp_port_phy_start(&pf->ptp.port, false);
	if (pf->ptp.port.link_up)
		ice_ptp_port_phy_start(&pf->ptp.port, true);

	ice_ptp_reset_ts_memory(pf);
	atomic_set(&pf->ptp.phy_reset_lock, false);
}

/**
 * ice_ptp_update_incval - Update clock increment rate
 * @pf: Board private structure
 * @time_ref_freq: TIME_REF frequency to use
 * @src_tmr_mode: Src timer mode (nanoseconds or locked)
 */
int
ice_ptp_update_incval(struct ice_pf *pf, enum ice_time_ref_freq time_ref_freq,
		      enum ice_src_tmr_mode src_tmr_mode)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	struct timespec64 ts;
	s64 incval;
	int err;

	if (!test_bit(ICE_FLAG_PTP, pf->flags)) {
		dev_err(dev, "PTP not ready, failed to update incval\n");
		return -EINVAL;
	}

	if ((time_ref_freq >= NUM_ICE_TIME_REF_FREQ ||
	     src_tmr_mode >= NUM_ICE_SRC_TMR_MODE))
		return -EINVAL;

	if (src_tmr_mode == ICE_SRC_TMR_MODE_NANOSECONDS)
		incval = ice_e822_nominal_incval(time_ref_freq);
	else
		incval = LOCKED_INCVAL_E822;

	if (!ice_ptp_lock(hw))
		return -EBUSY;

	status = ice_ptp_write_incval(hw, incval);
	if (status) {
		dev_err(dev, "PTP failed to update incval, status %s\n",
			ice_stat_str(status));
		err = ice_status_to_errno(status);
		goto err_unlock;
	}

	pf->ptp.time_ref_freq = time_ref_freq;
	pf->ptp.src_tmr_mode = src_tmr_mode;

	ts = ktime_to_timespec64(ktime_get_real());
	err = ice_ptp_write_init(pf, &ts);
	if (err) {
		dev_err(dev, "PTP failed to program time registers, err %d\n",
			err);
		goto err_unlock;
	}

	/* unlock PTP semaphore first before resetting PHY timestamping */
	ice_ptp_unlock(hw);
	ice_ptp_reset_phy_timestamping(pf);

	return 0;

err_unlock:
	ice_ptp_unlock(hw);

	return err;
}

#ifdef HAVE_PTP_CLOCK_INFO_ADJFINE
/**
 * ice_ptp_adjfine - Adjust clock increment rate
 * @info: the driver's PTP info structure
 * @scaled_ppm: Parts per million with 16-bit fractional field
 *
 * Adjust the frequency of the clock by the indicated scaled ppm from the
 * base frequency.
 */
static int ice_ptp_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	u64 freq, divisor = 1000000ULL;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	s64 incval, diff;
	int neg_adj = 0;

	if (pf->ptp.src_tmr_mode == ICE_SRC_TMR_MODE_LOCKED) {
		dev_err(ice_pf_to_dev(pf),
			"adjfreq not supported in locked mode\n");
		return -EPERM;
	}

	incval = ice_base_incval(pf);

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	while ((u64)scaled_ppm > div_u64(U64_MAX, incval)) {
		/* handle overflow by scaling down the scaled_ppm and
		 * the divisor, losing some precision
		 */
		scaled_ppm >>= 2;
		divisor >>= 2;
	}

	freq = (incval * (u64)scaled_ppm) >> 16;
	diff = div_u64(freq, divisor);

	if (neg_adj)
		incval -= diff;
	else
		incval += diff;

	status = ice_ptp_write_incval_locked(hw, incval);
	if (status) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to set incval, status %s\n",
			ice_stat_str(status));
		return -EIO;
	}

	return 0;
}

#else
/**
 * ice_ptp_adjfreq - Adjust the frequency of the clock
 * @info: the driver's PTP info structure
 * @ppb: Parts per billion adjustment from the base
 *
 * Adjust the frequency of the clock by the indicated parts per billion from the
 * base frequency.
 */
static int ice_ptp_adjfreq(struct ptp_clock_info *info, s32 ppb)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	s64 incval, freq, diff;

	if (pf->ptp.src_tmr_mode == ICE_SRC_TMR_MODE_LOCKED) {
		dev_err(ice_pf_to_dev(pf),
			"adjfreq not supported in locked mode\n");
		return -EPERM;
	}

	incval = ice_base_incval(pf);

	freq = incval * ppb;
	diff = div_s64(freq, 1000000000ULL);
	incval += diff;

	status = ice_ptp_write_incval_locked(hw, incval);
	if (status) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to set incval, status %s\n",
			ice_stat_str(status));
		return -EIO;
	}

	return 0;
}

#endif
/**
 * ice_ptp_extts_work - Workqueue task function
 * @pf: Board private structure
 *
 * Service for PTP external clock event
 */
static void ice_ptp_extts_work(struct ice_pf *pf)
{
	struct ptp_clock_event event;
	struct ice_hw *hw = &pf->hw;
	u8 chan, tmr_idx;
	u32 hi, lo;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	/* Event time is captured by one of the two matched registers
	 *      GLTSYN_EVNT_L: 32 LSB of sampled time event
	 *      GLTSYN_EVNT_H: 32 MSB of sampled time event
	 * Event is defined in GLTSYN_EVNT_0 register
	 */
	for (chan = 0; chan < GLTSYN_EVNT_H_IDX_MAX; chan++) {
		/* Check if channel is enabled */
		if (pf->ptp.ext_ts_irq & (1 << chan)) {
			lo = rd32(hw, GLTSYN_EVNT_L(chan, tmr_idx));
			hi = rd32(hw, GLTSYN_EVNT_H(chan, tmr_idx));
			event.timestamp = (((u64)hi) << 32) | lo;
			event.type = PTP_CLOCK_EXTTS;
			event.index = chan;

			/* Fire event */
			ptp_clock_event(pf->ptp.clock, &event);
			pf->ptp.ext_ts_irq &= ~(1 << chan);
		}
	}
}

/**
 * ice_ptp_cfg_extts - Configure EXTTS pin and channel
 * @pf: Board private structure
 * @ena: true to enable; false to disable
 * @chan: GPIO channel (0-3)
 * @gpio_pin: GPIO pin
 * @extts_flags: request flags from the ptp_extts_request.flags
 */
static int
ice_ptp_cfg_extts(struct ice_pf *pf, bool ena, unsigned int chan, u32 gpio_pin,
		  unsigned int extts_flags)
{
	u32 func, aux_reg, gpio_reg, irq_reg;
	struct ice_hw *hw = &pf->hw;
	u8 tmr_idx;

	if (pf->ptp.src_tmr_mode == ICE_SRC_TMR_MODE_LOCKED) {
		dev_err(ice_pf_to_dev(pf), "Locked mode EXTTS not supported\n");
		return -EOPNOTSUPP;
	}

	if (chan > (unsigned int)pf->ptp.info.n_ext_ts)
		return -EINVAL;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	irq_reg = rd32(hw, PFINT_OICR_ENA);

	if (ena) {
		/* Enable the interrupt */
		irq_reg |= PFINT_OICR_TSYN_EVNT_M;
		aux_reg = GLTSYN_AUX_IN_0_INT_ENA_M;

#define GLTSYN_AUX_IN_0_EVNTLVL_RISING_EDGE	BIT(0)
#define GLTSYN_AUX_IN_0_EVNTLVL_FALLING_EDGE	BIT(1)

		/* set event level to requested edge */
		if (extts_flags & PTP_FALLING_EDGE)
			aux_reg |= GLTSYN_AUX_IN_0_EVNTLVL_FALLING_EDGE;
		if (extts_flags & PTP_RISING_EDGE)
			aux_reg |= GLTSYN_AUX_IN_0_EVNTLVL_RISING_EDGE;

		/* Write GPIO CTL reg.
		 * 0x1 is input sampled by EVENT register(channel)
		 * + num_in_channels * tmr_idx
		 */
		func = 1 + chan + (tmr_idx * 3);
		gpio_reg = ((func << GLGEN_GPIO_CTL_PIN_FUNC_S) &
			    GLGEN_GPIO_CTL_PIN_FUNC_M);
		pf->ptp.ext_ts_chan |= (1 << chan);
	} else {
		/* clear the values we set to reset defaults */
		aux_reg = 0;
		gpio_reg = 0;
		pf->ptp.ext_ts_chan &= ~(1 << chan);
		if (!pf->ptp.ext_ts_chan)
			irq_reg &= ~PFINT_OICR_TSYN_EVNT_M;
	}

	wr32(hw, PFINT_OICR_ENA, irq_reg);
	wr32(hw, GLTSYN_AUX_IN(chan, tmr_idx), aux_reg);
	wr32(hw, GLGEN_GPIO_CTL(gpio_pin), gpio_reg);

	return 0;
}

/**
 * ice_ptp_cfg_clkout - Configure clock to generate periodic wave
 * @pf: Board private structure
 * @chan: GPIO channel (0-3)
 * @config: desired periodic clk configuration. NULL will disable channel
 * @store: If set to true the values will be stored
 *
 * Configure the internal clock generator modules to generate the clock wave of
 * specified period.
 */
int ice_ptp_cfg_clkout(struct ice_pf *pf, unsigned int chan,
		       struct ice_perout_channel *config, bool store)
{
	struct ice_hw *hw = &pf->hw;
	u64 current_time, period, start_time;
	u32 func, val, gpio_pin;
	u8 tmr_idx;

	if (pf->ptp.src_tmr_mode == ICE_SRC_TMR_MODE_LOCKED) {
		dev_err(ice_pf_to_dev(pf),
			"locked mode PPS/PEROUT not supported\n");
		return -EIO;
	}

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* 0. Reset mode & out_en in AUX_OUT */
	wr32(hw, GLTSYN_AUX_OUT(chan, tmr_idx), 0);

	/* If we're disabling the output, clear out CLKO and TGT and keep
	 * output level low
	 */
	if (!config || !config->ena) {
		wr32(hw, GLTSYN_CLKO(chan, tmr_idx), 0);
		wr32(hw, GLTSYN_TGT_L(chan, tmr_idx), 0);
		wr32(hw, GLTSYN_TGT_H(chan, tmr_idx), 0);

		val = GLGEN_GPIO_CTL_PIN_DIR_M;
		gpio_pin = pf->ptp.perout_channels[chan].gpio_pin;
		wr32(hw, GLGEN_GPIO_CTL(gpio_pin), val);

		/* Store the value if requested */
		if (store)
			memset(&pf->ptp.perout_channels[chan], 0,
			       sizeof(struct ice_perout_channel));

		return 0;
	}
	period = config->period;
	start_time = config->start_time;
	gpio_pin = config->gpio_pin;

	/* 1. Write clkout with half of required period value */
	if (period & 0x1) {
		dev_err(ice_pf_to_dev(pf), "CLK Period must be an even value\n");
		goto err;
	}

	period >>= 1;

	/* For proper operation, the GLTSYN_CLKO must be larger than clock tick
	 */
#define MIN_PULSE 3
	if (period <= MIN_PULSE || period > U32_MAX) {
		dev_err(ice_pf_to_dev(pf), "CLK Period must be > %d && < 2^33",
			MIN_PULSE * 2);
		goto err;
	}

	wr32(hw, GLTSYN_CLKO(chan, tmr_idx), lower_32_bits(period));

	/* Allow time for programming before start_time is hit */
	current_time = ice_ptp_read_src_clk_reg(pf, NULL);

	/* if start time is in the past start the timer at the nearest second
	 * maintaining phase
	 */
	if (start_time < current_time)
		start_time = roundup(current_time + NSEC_PER_MSEC,
				     NSEC_PER_SEC) + start_time % NSEC_PER_SEC;

	if (ice_is_e810(hw))
		start_time -= E810_OUT_PROP_DELAY_NS;
	else
		start_time -= ice_e822_pps_delay(pf->ptp.time_ref_freq);

	/* 2. Write TARGET time */
	wr32(hw, GLTSYN_TGT_L(chan, tmr_idx), lower_32_bits(start_time));
	wr32(hw, GLTSYN_TGT_H(chan, tmr_idx), upper_32_bits(start_time));

	/* 3. Write AUX_OUT register */
	val = GLTSYN_AUX_OUT_0_OUT_ENA_M | GLTSYN_AUX_OUT_0_OUTMOD_M;
	wr32(hw, GLTSYN_AUX_OUT(chan, tmr_idx), val);

	/* 4. write GPIO CTL reg */
	func = 8 + chan + (tmr_idx * 4);
	val = GLGEN_GPIO_CTL_PIN_DIR_M |
	      ((func << GLGEN_GPIO_CTL_PIN_FUNC_S) & GLGEN_GPIO_CTL_PIN_FUNC_M);
	wr32(hw, GLGEN_GPIO_CTL(gpio_pin), val);

	/* Store the value if requested */
	if (store) {
		memcpy(&pf->ptp.perout_channels[chan], config,
		       sizeof(struct ice_perout_channel));
		pf->ptp.perout_channels[chan].start_time %= NSEC_PER_SEC;
	}

	return 0;
err:
	dev_err(ice_pf_to_dev(pf), "PTP failed to cfg per_clk\n");
	return -EFAULT;
}

/**
 * ice_ptp_gettimex64 - Get the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure to hold the current time value
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 *
 * Read the device clock and return the correct value on ns, after converting it
 * into a timespec struct.
 */
static int
ice_ptp_gettimex64(struct ptp_clock_info *info, struct timespec64 *ts,
		   struct ptp_system_timestamp *sts)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;

	if (!ice_ptp_lock(hw)) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to get time\n");
		return -EBUSY;
	}

	ice_ptp_read_time(pf, ts, sts);
	ice_ptp_unlock(hw);

	return 0;
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIMEX64
/**
 * ice_ptp_gettime64 - Get the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure to hold the current time value
 *
 * Read the device clock and return the correct value on ns, after converting it
 * into a timespec struct.
 */
static int ice_ptp_gettime64(struct ptp_clock_info *info, struct timespec64 *ts)
{
	return ice_ptp_gettimex64(info, ts, NULL);
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIME64
/**
 * ice_ptp_gettime32 - Get the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec structure to hold the current time value
 *
 * Read the device clock and return the correct value on ns, after converting it
 * into a timespec struct.
 */
static int ice_ptp_gettime32(struct ptp_clock_info *info, struct timespec *ts)
{
	struct timespec64 ts64;

	if (ice_ptp_gettime64(info, &ts64))
		return -EFAULT;

	*ts = timespec64_to_timespec(ts64);
	return 0;
}

#endif /* !HAVE_PTP_CLOCK_INFO_GETTIME64 */
#endif /* !HAVE_PTP_CLOCK_INFO_GETTIMEX64 */
/**
 * ice_ptp_settime64 - Set the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure that holds the new time value
 *
 * Set the device clock to the user input value. The conversion from timespec
 * to ns happens in the write function.
 */
static int
ice_ptp_settime64(struct ptp_clock_info *info, const struct timespec64 *ts)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct timespec64 ts64 = *ts;
	struct ice_hw *hw = &pf->hw;
	u8 i;
	int err;

	/* For Vernier mode, we need to recalibrate after new settime
	 * Start with disabling timestamp block
	 */
	if (pf->ptp.port.link_up)
		ice_ptp_port_phy_start(&pf->ptp.port, false);

	if (!ice_ptp_lock(hw)) {
		err = -EBUSY;
		goto exit;
	}

	/* Disable periodic outputs */
	for (i = 0; i < info->n_per_out; i++)
		if (pf->ptp.perout_channels[i].ena)
			ice_ptp_cfg_clkout(pf, i, NULL, false);

	err = ice_ptp_write_init(pf, &ts64);
	ice_ptp_unlock(hw);

	if (!err)
		ice_ptp_update_cached_systime(pf);

	/* Reenable periodic outputs */
	for (i = 0; i < info->n_per_out; i++)
		if (pf->ptp.perout_channels[i].ena)
			ice_ptp_cfg_clkout(pf, i, &pf->ptp.perout_channels[i],
					   false);

	/* Recalibrate and re-enable timestamp block */
	if (pf->ptp.port.link_up)
		ice_ptp_port_phy_start(&pf->ptp.port, true);
exit:
	if (err) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to set time %d\n", err);
		return err;
	}

	return 0;
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIME64
/**
 * ice_ptp_settime32 - Set the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec structure that holds the new time value
 *
 * Set the device clock to the user input value. The conversion from timespec
 * to ns happens in the write function.
 */
static int
ice_ptp_settime32(struct ptp_clock_info *info, const struct timespec *ts)
{
	struct timespec64 ts64 = timespec_to_timespec64(*ts);

	return ice_ptp_settime64(info, &ts64);
}
#endif /* !HAVE_PTP_CLOCK_INFO_GETTIME64 */

/**
 * ice_ptp_adjtime_nonatomic - Do a non-atomic clock adjustment
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 */
static int ice_ptp_adjtime_nonatomic(struct ptp_clock_info *info, s64 delta)
{
	struct timespec64 now, then;

	then = ns_to_timespec64(delta);
	ice_ptp_gettimex64(info, &now, NULL);
	now = timespec64_add(now, then);

	return ice_ptp_settime64(info, (const struct timespec64 *)&now);
}


/**
 * ice_ptp_adjtime - Adjust the time of the clock by the indicated delta
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 */
static int ice_ptp_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;
	struct device *dev;
	int err;
	u8 i;

	dev = ice_pf_to_dev(pf);

	if (pf->ptp.src_tmr_mode == ICE_SRC_TMR_MODE_LOCKED) {
		dev_err(dev, "Locked Mode adjtime not supported\n");
		return -EIO;
	}

	/* Hardware only supports atomic adjustments using signed 32-bit
	 * integers. For any adjustment outside this range, perform
	 * a non-atomic get->adjust->set flow.
	 */
	if (delta > S32_MAX || delta < S32_MIN) {
		dev_dbg(dev, "delta = %lld, adjtime non-atomic\n", delta);
		return ice_ptp_adjtime_nonatomic(info, delta);
	}

	if (!ice_ptp_lock(hw)) {
		dev_err(dev, "PTP failed to acquire semaphore in adjtime\n");
		return -EBUSY;
	}

	/* Disable periodic outputs */
	for (i = 0; i < info->n_per_out; i++)
		if (pf->ptp.perout_channels[i].ena)
			ice_ptp_cfg_clkout(pf, i, NULL, false);

	err = ice_ptp_write_adj(pf, delta, true);

	/* Reenable periodic outputs */
	for (i = 0; i < info->n_per_out; i++)
		if (pf->ptp.perout_channels[i].ena)
			ice_ptp_cfg_clkout(pf, i, &pf->ptp.perout_channels[i],
					   false);

	ice_ptp_unlock(hw);

	/* Check error after restarting periodic outputs and releasing the PTP
	 * hardware lock.
	 */
	if (err) {
		dev_err(dev, "PTP failed to adjust time, err %d\n", err);
		return err;
	}

	ice_ptp_update_cached_systime(pf);

	return 0;
}

/**
 * ice_ptp_gpio_enable_e822 - Enable/disable ancillary features of PHC
 * @info: the driver's PTP info structure
 * @rq: The requested feature to change
 * @on: Enable/disable flag
 */
static int
ice_ptp_gpio_enable_e822(struct ptp_clock_info *info,
			 struct ptp_clock_request *rq, int on)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_perout_channel clk_cfg = {0};
	int err;

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		clk_cfg.gpio_pin = PPS_PIN_INDEX;
		clk_cfg.period = ((rq->perout.period.sec * NSEC_PER_SEC) +
				   rq->perout.period.nsec);
		clk_cfg.start_time = ((rq->perout.start.sec * NSEC_PER_SEC) +
				       rq->perout.start.nsec);
		clk_cfg.ena = !!on;

		err = ice_ptp_cfg_clkout(pf, rq->perout.index, &clk_cfg, true);
		break;
	case PTP_CLK_REQ_EXTTS:
		err = ice_ptp_cfg_extts(pf, !!on, rq->extts.index,
					TIME_SYNC_PIN_INDEX, rq->extts.flags);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

/**
 * ice_ptp_gpio_enable_e810 - Enable/disable ancillary features of PHC
 * @info: the driver's PTP info structure
 * @rq: The requested feature to change
 * @on: Enable/disable flag
 */
static int
ice_ptp_gpio_enable_e810(struct ptp_clock_info *info,
			 struct ptp_clock_request *rq, int on)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_perout_channel clk_cfg = {0};
	unsigned int chan;
	u32 gpio_pin;
	int err;

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		chan = rq->perout.index;
		if (ice_is_e810t(&pf->hw)) {
			if (chan == ice_e810t_pin_desc[SMA1].chan)
				clk_cfg.gpio_pin = GPIO_20;
			else if (chan == ice_e810t_pin_desc[SMA2].chan)
				clk_cfg.gpio_pin = GPIO_22;
			else
				return -1;
		} else if (chan == PPS_CLK_GEN_CHAN) {
			clk_cfg.gpio_pin = PPS_PIN_INDEX;
		} else {
			clk_cfg.gpio_pin = chan;
		}

		clk_cfg.period = ((rq->perout.period.sec * NSEC_PER_SEC) +
				   rq->perout.period.nsec);
		clk_cfg.start_time = ((rq->perout.start.sec * NSEC_PER_SEC) +
				       rq->perout.start.nsec);
		clk_cfg.ena = !!on;

		err = ice_ptp_cfg_clkout(pf, chan, &clk_cfg, true);
		break;
	case PTP_CLK_REQ_EXTTS:
		chan = rq->extts.index;
		if (ice_is_e810t(&pf->hw)) {
			if (chan < 2)
				gpio_pin = GPIO_21;
			else
				gpio_pin = GPIO_23;
		} else {
			gpio_pin = chan;
		}

		err = ice_ptp_cfg_extts(pf, !!on, chan, gpio_pin,
					rq->extts.flags);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

#ifdef HAVE_PTP_CROSSTIMESTAMP
/**
 * ice_ptp_get_syncdevicetime - Get the cross time stamp info
 * @device: Current device time
 * @system: System counter value read synchronously with device time
 * @ctx: Context provided by timekeeping code
 *
 * Read device and system (ART) clock simultaneously and return the corrected
 * clock values in ns.
 */
static int
ice_ptp_get_syncdevicetime(ktime_t *device,
			   struct system_counterval_t *system,
			   void *ctx)
{
	struct ice_pf *pf = (struct ice_pf *)ctx;
	struct ice_hw *hw = &pf->hw;
	u32 hh_lock, hh_art_ctl;
	int i;

	/* Get the HW lock */
	hh_lock = rd32(hw, PFHH_SEM + (PFTSYN_SEM_BYTES * hw->pf_id));
	if (hh_lock & PFHH_SEM_BUSY_M) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to get hh lock\n");
		return -EFAULT;
	}

	/* Start the ART and device clock sync sequence */
	hh_art_ctl = rd32(hw, GLHH_ART_CTL);
	hh_art_ctl = hh_art_ctl | GLHH_ART_CTL_ACTIVE_M;
	wr32(hw, GLHH_ART_CTL, hh_art_ctl);

#define MAX_HH_LOCK_TRIES 100

	for (i = 0; i < MAX_HH_LOCK_TRIES; i++) {
		/* Wait for sync to complete */
		hh_art_ctl = rd32(hw, GLHH_ART_CTL);
		if (hh_art_ctl & GLHH_ART_CTL_ACTIVE_M) {
			udelay(1);
			continue;
		} else {
			u32 hh_ts_lo, hh_ts_hi, tmr_idx;
			u64 hh_ts;

			tmr_idx = hw->func_caps.ts_func_info.tmr_index_assoc;
			/* Read ART time */
			hh_ts_lo = rd32(hw, GLHH_ART_TIME_L);
			hh_ts_hi = rd32(hw, GLHH_ART_TIME_H);
			hh_ts = ((u64)hh_ts_hi << 32) | hh_ts_lo;
			*system = convert_art_ns_to_tsc(hh_ts);
			/* Read Device source clock time */
			hh_ts_lo = rd32(hw, GLTSYN_HHTIME_L(tmr_idx));
			hh_ts_hi = rd32(hw, GLTSYN_HHTIME_H(tmr_idx));
			hh_ts = ((u64)hh_ts_hi << 32) | hh_ts_lo;
			*device = ns_to_ktime(hh_ts);
			break;
		}
	}
	/* Release HW lock */
	hh_lock = rd32(hw, PFHH_SEM + (PFTSYN_SEM_BYTES * hw->pf_id));
	hh_lock = hh_lock & ~PFHH_SEM_BUSY_M;
	wr32(hw, PFHH_SEM + (PFTSYN_SEM_BYTES * hw->pf_id), hh_lock);

	if (i == MAX_HH_LOCK_TRIES)
		return -ETIMEDOUT;

	return 0;
}

/**
 * ice_ptp_getcrosststamp_e822 - Capture a device cross timestamp
 * @info: the driver's PTP info structure
 * @cts: The memory to fill the cross timestamp info
 *
 * Capture a cross timestamp between the ART and the device PTP hardware
 * clock. Fill the cross timestamp information and report it back to the
 * caller.
 *
 * This is only valid for E822 devices which have support for generating the
 * cross timestamp via PCIe PTM.
 *
 * In order to correctly correlate the ART timestamp back to the TSC time, the
 * CPU must have X86_FEATURE_TSC_KNOWN_FREQ.
 */
static int
ice_ptp_getcrosststamp_e822(struct ptp_clock_info *info,
			    struct system_device_crosststamp *cts)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	return get_device_system_crosststamp(ice_ptp_get_syncdevicetime,
					     pf, NULL, cts);
}
#endif /* HAVE_PTP_CROSSTIMESTAMP */

/**
 * ice_ptp_set_timestamp_mode - Setup driver for requested timestamp mode
 * @pf: Board private structure
 * @config: hwtstamp settings requested or saved
 */
static int
ice_ptp_set_timestamp_mode(struct ice_pf *pf, struct hwtstamp_config *config)
{
	/* Reserved for future extensions. */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		ice_set_tx_tstamp(pf, false);
		break;
	case HWTSTAMP_TX_ON:
		ice_set_tx_tstamp(pf, true);
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ice_set_rx_tstamp(pf, false);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
#ifdef HAVE_HWTSTAMP_FILTER_NTP_ALL
	case HWTSTAMP_FILTER_NTP_ALL:
#endif /* HAVE_HWTSTAMP_FILTER_NTP_ALL */
	case HWTSTAMP_FILTER_ALL:
		ice_set_rx_tstamp(pf, true);
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

/**
 * ice_ptp_get_ts_config - ioctl interface to read the timestamping config
 * @pf: Board private structure
 * @ifr: ioctl data
 *
 * Copy the timestamping config to user buffer
 */
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	struct hwtstamp_config *config;

	if (!test_bit(ICE_FLAG_PTP, pf->flags))
		return -EIO;

	config = &pf->ptp.tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/**
 * ice_ptp_set_ts_config - ioctl interface to control the timestamping
 * @pf: Board private structure
 * @ifr: ioctl data
 *
 * Get the user config and store it
 */
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (!test_bit(ICE_FLAG_PTP, pf->flags))
		return -EAGAIN;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = ice_ptp_set_timestamp_mode(pf, &config);
	if (err)
		return err;

	/* Save these settings for future reference */
	pf->ptp.tstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * ice_ptp_get_tx_hwtstamp_ver - Returns the Tx timestamp and valid bits
 * @pf: Board specific private structure
 * @tx_idx_req: Bitmap of timestamp indices to read
 * @quad: Quad to read
 * @ts: Timestamps read from PHY
 * @ts_read: On return, if non-NULL: bitmap of read timestamp indices
 *
 * Read the value of the Tx timestamp from the registers and build a
 * bitmap of successfully read indices and count of the number successfully
 * read.
 *
 * There are 3 possible return values,
 * 0 = success
 *
 * -EIO = unable to read a register, this could be to a variety of issues but
 *  should be very rare.  Up to caller how to respond to this (retry, abandon,
 *  etc).  But once this situation occurs, stop reading as we cannot
 *  guarantee what state the PHY or Timestamp Unit is in.
 *
 * -EINVAL = (at least) one of the timestamps that was read did not have the
 *  TS_VALID bit set, and is probably zero.  Be aware that not all of the
 *  timestamps that were read (so the TS_READY bit for this timestamp was
 *  cleared but no valid TS was retrieved) are present.  Expect at least one
 *  ts_read index that should be 1 is zero.
 */
static int ice_ptp_get_tx_hwtstamp_ver(struct ice_pf *pf, u64 tx_idx_req,
				       u8 quad, u64 *ts, u64 *ts_read)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	unsigned long i;
	u64 ts_ns;


	for_each_set_bit(i, (unsigned long *)&tx_idx_req, INDEX_PER_QUAD) {
		ts[i] = 0x0;

		status = ice_read_phy_tstamp(hw, quad, i, &ts_ns);
		if (status) {
			dev_dbg(dev, "PTP Tx read failed, status %s\n",
				ice_stat_str(status));
			return ice_status_to_errno(status);
		}

		if (ts_read)
			*ts_read |= BIT(i);

		if (!(ts_ns & ICE_PTP_TS_VALID)) {
			dev_dbg(dev, "PTP tx invalid\n");
			continue;
		}

		ts_ns = ice_ptp_extend_40b_ts(pf, ts_ns);
		/* Each timestamp will be offset in the array of
		 * timestamps by the index's value.  So the timestamp
		 * from index n will be in ts[n] position.
		 */
		ts[i] = ts_ns;
	}

	return 0;
}


/**
 * ice_ptp_get_tx_hwtstamp_ready - Get the Tx timestamp ready bitmap
 * @pf: The PF private data structure
 * @quad: Quad to read (0-4)
 * @ts_ready: Bitmap where each bit set indicates that the corresponding
 *            timestamp register is ready to read
 *
 * Read the PHY timestamp ready registers for a particular bank.
 */
static void
ice_ptp_get_tx_hwtstamp_ready(struct ice_pf *pf, u8 quad, u64 *ts_ready)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u64 bitmap;
	u32 val;

	status = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEMORY_STATUS_U,
					&val);
	if (status) {
		dev_dbg(dev, "TX_MEMORY_STATUS_U read failed for quad %u\n",
			quad);
		return;
	}

	bitmap = val;

	status = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEMORY_STATUS_L,
					&val);
	if (status) {
		dev_dbg(dev, "TX_MEMORY_STATUS_L read failed for quad %u\n",
			quad);
		return;
	}

	bitmap = (bitmap << 32) | val;

	*ts_ready = bitmap;

}

/**
 * ice_ptp_tx_hwtstamp_vsi - Return the Tx timestamp for a specified VSI
 * @vsi: lport corresponding VSI
 * @idx: Index of timestamp read from QUAD memory
 * @hwtstamp: Timestamps read from PHY
 *
 * Helper function for ice_ptp_tx_hwtstamp.
 */
static void
ice_ptp_tx_hwtstamp_vsi(struct ice_vsi *vsi, int idx, u64 hwtstamp)
{
	struct skb_shared_hwtstamps shhwtstamps = {};
	struct sk_buff *skb;

	skb = vsi->ptp_tx_skb[idx];
	if (!skb)
		return;

	shhwtstamps.hwtstamp = ns_to_ktime(hwtstamp);

	vsi->ptp_tx_skb[idx] = NULL;

	/* Notify the stack and free the skb after we've unlocked */
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
	clear_bit(idx, vsi->ptp_tx_idx);
}

/**
 * ice_ptp_tx_hwtstamp - Return the Tx timestamps
 * @pf: Board private structure
 *
 * Read the tx_memory_status registers for the PHY timestamp block. Determine
 * which entries contain a valid ready timestamp. Read out the timestamp from
 * the table. Convert the 40b timestamp value into the 64b nanosecond value
 * consumed by the stack, and then report it as part of the related skb's
 * shhwtstamps structure.
 *
 * Note that new timestamps might come in while we're reading the timestamp
 * block. However, no interrupts will be triggered until the intr_threshold is
 * crossed again. Thus we read the status registers in a loop until no more
 * timestamps are ready.
 */
static void ice_ptp_tx_hwtstamp(struct ice_pf *pf)
{
	u8 quad, lport, qport;
	struct ice_vsi *vsi;
	int msk_shft;
	u64 rdy_msk;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;

	lport = vsi->port_info->lport;
	qport = lport % ICE_PORTS_PER_QUAD;
	quad = lport / ICE_PORTS_PER_QUAD;
	msk_shft = qport * INDEX_PER_PORT;
	rdy_msk = GENMASK_ULL(msk_shft + INDEX_PER_PORT - 1, msk_shft);

	while (true) {
		u64 ready_map = 0, valid_map = 0;
		u64 hwtstamps[INDEX_PER_QUAD];
		int i, ret;

		ice_ptp_get_tx_hwtstamp_ready(pf, quad, &ready_map);
		ready_map &= rdy_msk;
		if (!ready_map)
			break;

		ret = ice_ptp_get_tx_hwtstamp_ver(pf, ready_map, quad,
						  hwtstamps, &valid_map);
		if (ret == -EIO)
			break;

		for_each_set_bit(i, (unsigned long *)&valid_map, INDEX_PER_QUAD)
			if (test_bit(i, vsi->ptp_tx_idx))
				ice_ptp_tx_hwtstamp_vsi(vsi, i, hwtstamps[i]);
	}
}

/**
 * ice_ptp_tx_hwtstamp_ext - Return the Tx timestamp
 * @pf: Board private structure
 *
 * Read the value of the Tx timestamp from the registers, convert it into
 * a value consumable by the stack, and store that result into the shhwtstamps
 * struct before returning it up the stack.
 */
static void ice_ptp_tx_hwtstamp_ext(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	struct ice_vsi *vsi;
	u8 lport;
	int idx;

	vsi = ice_get_main_vsi(pf);
	if (!vsi || !vsi->ptp_tx)
		return;
	lport = hw->port_info->lport;

	/* Don't attempt to timestamp if we don't have an skb */
	for (idx = 0; idx < INDEX_PER_QUAD; idx++) {
		struct skb_shared_hwtstamps shhwtstamps = {};
		enum ice_status status;
		struct sk_buff *skb;
		u64 ts_ns;

		skb = vsi->ptp_tx_skb[idx];
		if (!skb)
			continue;

		status = ice_read_phy_tstamp(hw, lport, idx, &ts_ns);
		if (status) {
			dev_err(ice_pf_to_dev(pf), "PTP tx rd failed, status %s\n",
				ice_stat_str(status));
			vsi->ptp_tx_skb[idx] = NULL;
			dev_kfree_skb_any(skb);
			clear_bit(idx, vsi->ptp_tx_idx);
		}

		ts_ns = ice_ptp_extend_40b_ts(pf, ts_ns);

		shhwtstamps.hwtstamp = ns_to_ktime(ts_ns);

		vsi->ptp_tx_skb[idx] = NULL;

		/* Notify the stack and free the skb after
		 * we've unlocked
		 */
		skb_tstamp_tx(skb, &shhwtstamps);
		dev_kfree_skb_any(skb);
		clear_bit(idx, vsi->ptp_tx_idx);
	}
}

/**
 * ice_ptp_rx_hwtstamp - Check for an Rx timestamp
 * @rx_ring: Ring to get the VSI info
 * @rx_desc: Receive descriptor
 * @skb: Particular skb to send timestamp with
 *
 * The driver receives a notification in the receive descriptor with timestamp.
 * The timestamp is in ns, so we must convert the result first.
 */
void ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
			 union ice_32b_rx_flex_desc *rx_desc,
			 struct sk_buff *skb)
{
	u32 ts_high;
	u64 ts_ns;

	/* Populate timesync data into skb */
	if (rx_desc->wb.time_stamp_low & ICE_PTP_TS_VALID) {
		struct skb_shared_hwtstamps *hwtstamps;

		/* Use ice_ptp_extend_32b_ts directly, using the ring-specific
		 * cached PHC value, rather than accessing the PF. This also
		 * allows us to simply pass the upper 32bits of nanoseconds
		 * directly. Calling ice_ptp_extend_40b_ts is unnecessary as
		 * it would just discard these bits itself.
		 */
		ts_high = le32_to_cpu(rx_desc->wb.flex_ts.ts_high);
		ts_ns = ice_ptp_extend_32b_ts(rx_ring->cached_systime, ts_high);

		hwtstamps = skb_hwtstamps(skb);
		memset(hwtstamps, 0, sizeof(*hwtstamps));
		hwtstamps->hwtstamp = ns_to_ktime(ts_ns);
	}
}

/**
 * ice_ptp_setup_pins_e810t - Setup PTP pins in sysfs
 * @pf: pointer to the PF instance
 * @info: PTP clock capabilities
 */
static void
ice_ptp_setup_pins_e810t(struct ice_pf *pf, struct ptp_clock_info *info)
{
	info->n_per_out = E810T_N_PER_OUT;

	if (!ice_is_feature_supported(pf, ICE_F_PTP_EXTTS))
		return;

	info->n_ext_ts = E810_N_EXT_TS;
	info->n_pins = NUM_E810T_PTP_PINS;
	info->verify = ice_e810t_verify_pin;
}

/**
 * ice_ptp_setup_pins_e810 - Setup PTP pins in sysfs
 * @pf: pointer to the PF instance
 * @info: PTP clock capabilities
 */
static void
ice_ptp_setup_pins_e810(struct ice_pf *pf, struct ptp_clock_info *info)
{
	info->n_per_out = E810_N_PER_OUT;

	if (!ice_is_feature_supported(pf, ICE_F_PTP_EXTTS))
		return;

	info->n_ext_ts = E810_N_EXT_TS;
}

/**
 * ice_ptp_setup_pins_e822 - Setup PTP pins in sysfs
 * @pf: pointer to the PF instance
 * @info: PTP clock capabilities
 */
static void
ice_ptp_setup_pins_e822(struct ice_pf *pf, struct ptp_clock_info *info)
{
	info->pps = 1;
	info->n_per_out = 1;
	if (!ice_is_feature_supported(pf, ICE_F_PTP_EXTTS))
		return;
	info->n_ext_ts = 1;
}

/**
 * ice_ptp_set_funcs_e822 - Set specialized functions for E822 support
 * @pf: Board private structure
 * @info: PTP info to fill
 *
 * Assign functions to the PTP capabiltiies structure for E822 devices.
 * Functions which operate across all device families should be set directly
 * in ice_ptp_set_caps. Only add functions here which are distinct for E822
 * devices.
 */
static void
ice_ptp_set_funcs_e822(struct ice_pf *pf, struct ptp_clock_info *info)
{
#ifdef HAVE_PTP_CROSSTIMESTAMP
	if (boot_cpu_has(X86_FEATURE_ART) &&
	    boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ))
		info->getcrosststamp = ice_ptp_getcrosststamp_e822;
#endif /* HAVE_PTP_CROSSTIMESTAMP */
	info->enable = ice_ptp_gpio_enable_e822;

	ice_ptp_setup_pins_e822(pf, info);
}

/**
 * ice_ptp_set_funcs_e810 - Set specialized functions for E810 support
 * @pf: Board private structure
 * @info: PTP info to fill
 *
 * Assign functions to the PTP capabiltiies structure for E810 devices.
 * Functions which operate across all device families should be set directly
 * in ice_ptp_set_caps. Only add functions here which are distinct for e810
 * devices.
 */
static void
ice_ptp_set_funcs_e810(struct ice_pf *pf, struct ptp_clock_info *info)
{
	info->enable = ice_ptp_gpio_enable_e810;

	if (ice_is_e810t(&pf->hw))
		ice_ptp_setup_pins_e810t(pf, info);
	else
		ice_ptp_setup_pins_e810(pf, info);
}

/**
 * ice_ptp_set_caps - Set PTP capabilities
 * @pf: Board private structure
 */
static void ice_ptp_set_caps(struct ice_pf *pf)
{
	struct ptp_clock_info *info = &pf->ptp.info;
	struct device *dev = ice_pf_to_dev(pf);

	snprintf(info->name, sizeof(info->name) - 1, "%s-%s-clk",
		 dev_driver_string(dev), dev_name(dev));
	info->owner = THIS_MODULE;
	info->max_adj = 999999999;
	info->adjtime = ice_ptp_adjtime;
#ifdef HAVE_PTP_CLOCK_INFO_ADJFINE
	info->adjfine = ice_ptp_adjfine;
#else
	info->adjfreq = ice_ptp_adjfreq;
#endif
#if defined(HAVE_PTP_CLOCK_INFO_GETTIMEX64)
	info->gettimex64 = ice_ptp_gettimex64;
#elif defined(HAVE_PTP_CLOCK_INFO_GETTIME64)
	info->gettime64 = ice_ptp_gettime64;
#else
	info->gettime = ice_ptp_gettime32;
#endif
#ifdef HAVE_PTP_CLOCK_INFO_GETTIME64
	info->settime64 = ice_ptp_settime64;
#else
	info->settime = ice_ptp_settime32;
#endif /* HAVE_PTP_CLOCK_INFO_GETTIME64 */

	if (ice_is_e810(&pf->hw))
		ice_ptp_set_funcs_e810(pf, info);
	else
		ice_ptp_set_funcs_e822(pf, info);
}

/**
 * ice_ptp_create_clock - Create PTP clock device for userspace
 * @pf: Board private structure
 *
 * This function creates a new PTP clock device. It only creates one if we
 * don't already have one. Will return error if it can't create one, but success
 * if we already have a device. Should be used by ice_ptp_init to create clock
 * initially, and prevent global resets from creating new clock devices.
 */
static long ice_ptp_create_clock(struct ice_pf *pf)
{
	struct ptp_clock_info *info;
	struct ptp_clock *clock;
	struct device *dev;

	/* No need to create a clock device if we already have one */
	if (pf->ptp.clock)
		return 0;

	ice_ptp_set_caps(pf);

	info = &pf->ptp.info;
	dev = ice_pf_to_dev(pf);

	/* Allocate memory for kernel pins interface */
	if (info->n_pins) {
		info->pin_config = devm_kcalloc(dev, info->n_pins,
						sizeof(*info->pin_config),
						GFP_KERNEL);
		if (!info->pin_config) {
			info->n_pins = 0;
			return ICE_ERR_NO_MEMORY;
		}
	}

	if (ice_is_e810t(&pf->hw)) {
		/* Enable SMA controller */
		int err = ice_enable_e810t_sma_ctrl(&pf->hw, true);

		if (err)
			return err;

		/* Read current SMA status */
		err = ice_get_e810t_sma_config(&pf->hw, info->pin_config);
		if (err)
			return err;
	}

	/* Attempt to register the clock before enabling the hardware. */
	clock = ptp_clock_register(info, dev);
	if (IS_ERR(clock))
		return PTR_ERR(clock);

	pf->ptp.clock = clock;

	return 0;
}

/**
 * ice_ptp_init_owner - Initialize PTP_1588_CLOCK device
 * @pf: Board private structure
 *
 * Setup and initialize a PTP clock device that represents the device hardware
 * clock. Save the clock index for other functions connected to the same
 * hardware resource.
 */
static int ice_ptp_init_owner(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	struct timespec64 ts;
	int err, itr = 1;
	u8 src_idx;
	u32 regval;

	if (ice_is_e810(hw))
		wr32(hw, GLTSYN_SYNC_DLAY, 0);

	/* Clear some HW residue and enable source clock */
	src_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Enable source clocks */
	wr32(hw, GLTSYN_ENA(src_idx), GLTSYN_ENA_TSYN_ENA_M);

	if (ice_is_e810(hw)) {
		/* Enable PHY time sync */
		status = ice_ptp_init_phy_e810(hw);
		if (status) {
			err = ice_status_to_errno(status);
			goto err_exit;
		}
	}

	/* Clear event status indications for auxiliary pins */
	(void)rd32(hw, GLTSYN_STAT(src_idx));

#define PF_SB_REM_DEV_CTL_PHY0	BIT(2)
	if (!ice_is_e810(hw)) {
		regval = rd32(hw, PF_SB_REM_DEV_CTL);
		regval |= PF_SB_REM_DEV_CTL_PHY0;
		wr32(hw, PF_SB_REM_DEV_CTL, regval);
	}

	/* Acquire the global hardware lock */
	if (!ice_ptp_lock(hw)) {
		err = -EBUSY;
		goto err_exit;
	}

	/* Write the increment time value to PHY and LAN */
	status = ice_ptp_write_incval(hw, ice_base_incval(pf));
	if (status) {
		err = ice_status_to_errno(status);
		ice_ptp_unlock(hw);
		goto err_exit;
	}

	ts = ktime_to_timespec64(ktime_get_real());
	/* Write the initial Time value to PHY and LAN */
	err = ice_ptp_write_init(pf, &ts);
	if (err) {
		ice_ptp_unlock(hw);
		goto err_exit;
	}

	/* Release the global hardware lock */
	ice_ptp_unlock(hw);

	if (!ice_is_e810(hw)) {
		/* Set window length for all the ports */
		status = ice_ptp_set_vernier_wl(hw);
		if (status)  {
			err = ice_status_to_errno(status);
			goto err_exit;
		}

		/* Enable quad interrupts */
		err = ice_ptp_tx_ena_intr(pf, true, itr);
		if (err)
			goto err_exit;

		/* Reset timestamping memory in QUADs */
		ice_ptp_reset_ts_memory(pf);
	}

	/* Ensure we have a clock device */
	err = ice_ptp_create_clock(pf);
	if (err)
		goto err_clk;

	/* Store the PTP clock index for other PFs */
	ice_set_ptp_clock_index(pf);

	return 0;

err_clk:
	pf->ptp.clock = NULL;
err_exit:
	dev_err(dev, "PTP failed to register clock, err %d\n", err);

	return err;
}

/**
 * ice_ptp_init - Initialize PTP hardware clock support
 * @pf: Board private structure
 *
 * Setup the device for interacting with the PTP hardware clock for all
 * functions, both the function that owns the clock hardware, and the
 * functions connected to the clock hardware.
 *
 * The clock owner will allocate and register a ptp_clock with the
 * PTP_1588_CLOCK infrastructure. All functions allocate a kthread and work
 * items used for asynchronous work such as Tx timestamps and periodic work.
 */
void ice_ptp_init(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	int err;


	/* If this function owns the clock hardware, it must allocate and
	 * configure the PTP clock device to represent it.
	 */
	if (hw->func_caps.ts_func_info.src_tmr_owned) {
		err = ice_ptp_init_owner(pf);
		if (err)
			return;
	}

	/* Disable timestamping for both Tx and Rx */
	ice_ptp_cfg_timestamp(pf, false);

	/* Initialize work structures */
	mutex_init(&pf->ptp.port.ps_lock);
	pf->ptp.port.link_up = false;
	pf->ptp.port.port_num = pf->hw.pf_id;
	INIT_WORK(&pf->ptp.port.ov_task, ice_ptp_wait_for_offset_valid);

	/* Allocate workqueue for 2nd part of Vernier calibration */
	pf->ptp.ov_wq = alloc_workqueue("%s_ov", WQ_MEM_RECLAIM, 0,
					KBUILD_MODNAME);
	if (!pf->ptp.ov_wq) {
		err = -ENOMEM;
		goto err_wq;
	}

	set_bit(ICE_FLAG_PTP, pf->flags);
	dev_info(dev, "PTP init successful\n");

	if (hw->func_caps.ts_func_info.src_tmr_owned && !ice_is_e810(hw))
		ice_cgu_init_state(pf);
	return;

err_wq:
	/* If we registered a PTP clock, release it */
	if (pf->ptp.clock) {
		ptp_clock_unregister(pf->ptp.clock);
		pf->ptp.clock = NULL;
	}
	dev_err(dev, "PTP failed %d\n", err);
}

/**
 * ice_ptp_release - Disable the driver/HW support and unregister the clock
 * @pf: Board private structure
 *
 * This function handles the cleanup work required from the initialization by
 * clearing out the important information and unregistering the clock
 */
void ice_ptp_release(struct ice_pf *pf)
{
	struct ice_vsi *vsi;
	char *dev_name;
	u8 quad, i;

	if (!pf)
		return;

	vsi = ice_get_main_vsi(pf);
	if (!vsi || !test_bit(ICE_FLAG_PTP, pf->flags))
		return;

	dev_name = vsi->netdev->name;

	/* Disable timestamping for both Tx and Rx */
	ice_ptp_cfg_timestamp(pf, false);
	/* Clear PHY bank residues if any */
	quad = vsi->port_info->lport / ICE_PORTS_PER_QUAD;

	if (!ice_is_e810(&pf->hw) && !pf->hw.reset_ongoing) {
		u64 tx_idx = ~((u64)0);
		u64 ts[INDEX_PER_QUAD];

		ice_ptp_get_tx_hwtstamp_ver(pf, tx_idx, quad, ts, NULL);
	} else {
		ice_ptp_tx_hwtstamp_ext(pf);
	}

	/* Release any pending skb */
	ice_ptp_rel_all_skb(pf);

	clear_bit(ICE_FLAG_PTP, pf->flags);

	pf->ptp.port.link_up = false;
	if (pf->ptp.ov_wq) {
		destroy_workqueue(pf->ptp.ov_wq);
		pf->ptp.ov_wq = NULL;
	}

	if (!pf->ptp.clock)
		return;

	/* Disable periodic outputs */
	for (i = 0; i < pf->ptp.info.n_per_out; i++)
		if (pf->ptp.perout_channels[i].ena)
			ice_ptp_cfg_clkout(pf, i, NULL, false);

	ice_clear_ptp_clock_index(pf);
	ptp_clock_unregister(pf->ptp.clock);
	pf->ptp.clock = NULL;

	/* Free pin config */
	if (pf->ptp.info.pin_config) {
		devm_kfree(ice_pf_to_dev(pf), pf->ptp.info.pin_config);
		pf->ptp.info.pin_config = NULL;
	}

	dev_info(ice_pf_to_dev(pf), "removed Clock from %s\n", dev_name);
}

/**
 * ice_ptp_set_timestamp_offsets - Calculate timestamp offsets on each port
 * @pf: Board private structure
 *
 * This function calculates timestamp Tx/Rx offset on each port after at least
 * one packet was sent/received by the PHY.
 */
void ice_ptp_set_timestamp_offsets(struct ice_pf *pf)
{
	if (!test_bit(ICE_FLAG_PTP, pf->flags))
		return;

	if (atomic_read(&pf->ptp.phy_reset_lock))
		return;

	ice_ptp_check_offset_valid(&pf->ptp.port);
}

/**
 * ice_clean_ptp_subtask - Handle the service task events
 * @pf: Board private structure
 */
void ice_clean_ptp_subtask(struct ice_pf *pf)
{
	if (!test_bit(ICE_FLAG_PTP, pf->flags))
		return;

	ice_ptp_update_cached_systime(pf);
	if (test_and_clear_bit(ICE_PTP_EXT_TS_READY, pf->state))
		ice_ptp_extts_work(pf);
	if (test_and_clear_bit(ICE_PTP_TX_TS_READY, pf->state)) {
		struct ice_hw *hw = &pf->hw;

		if (ice_is_e810(hw))
			ice_ptp_tx_hwtstamp_ext(pf);
		else
			ice_ptp_tx_hwtstamp(pf);
	}
}
