// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice_type.h"
#include "ice_common.h"
#include "ice_ptp_hw.h"
#include "ice_ptp_consts.h"

/* Low level functions for interacting with and managing the device clock used
 * for the Precision Time Protocol.
 *
 * The ice hardware represents the current time using three registers:
 *
 *    GLTSYN_TIME_H     GLTSYN_TIME_L     GLTSYN_TIME_R
 *  +---------------+ +---------------+ +---------------+
 *  |    32 bits    | |    32 bits    | |    32 bits    |
 *  +---------------+ +---------------+ +---------------+
 *
 * The registers are incremented every clock tick using a 40bit increment
 * value defined over two registers:
 *
 *                     GLTSYN_INCVAL_H   GLTSYN_INCVAL_L
 *                    +---------------+ +---------------+
 *                    |    8 bit s    | |    32 bits    |
 *                    +---------------+ +---------------+
 *
 * The increment value is added to the GLSTYN_TIME_R and GLSTYN_TIME_L
 * registers every clock source tick. Depending on the specific device
 * configuration, the clock source frequency could be one of a number of
 * values.
 *
 * For E810 devices, the increment frequency is 812.5 MHz
 *
 * For E822 devices the clock can be derived from different sources, and the
 * increment has an effective frequency of one of the following:
 * - 823.4375 MHz
 * - 783.36 MHz
 * - 796.875 MHz
 * - 816 MHz
 * - 830.078125 MHz
 * - 783.36 MHz
 *
 * The hardware captures timestamps in the PHY for incoming packets, and for
 * outgoing packets on request. To support this, the PHY maintains a timer
 * that matches the lower 64 bits of the global source timer.
 *
 * In order to ensure that the PHY timers and the source timer are equivalent,
 * shadow registers are used to prepare the desired initial values. A special
 * sync command is issued to trigger copying from the shadow registers into
 * the appropriate source and PHY registers simultaneously.
 *
 * The driver supports devices which have different PHYs with subtly different
 * mechanisms to program and control the timers. We divide the devices into
 * families named after the first major device, E810 and similar devices, and
 * E822 and similar devices.
 *
 * - E822 based devices have additional support for fine grained Vernier
 *   calibration which requires significant setup
 * - The layout of timestamp data in the PHY register blocks is different
 * - The way timer synchronization commands are issued is different.
 *
 * To support this, very low level functions have an e810 or e822 suffix
 * indicating what type of device they work on. Higher level abstractions for
 * tasks that can be done on both devices do not have the suffix and will
 * correctly look up the appropriate low level function when running.
 *
 * Functions which only make sense on a single device family may not have
 * a suitable generic implementation
 */

/**
 * ice_get_ptp_src_clock_index - determine source clock index
 * @hw: pointer to HW struct
 *
 * Determine the source clock index currently in use, based on device
 * capabilities reported during initialization.
 */
u8 ice_get_ptp_src_clock_index(struct ice_hw *hw)
{
	return hw->func_caps.ts_func_info.tmr_index_assoc;
}

/**
 * ice_ptp_read_src_incval - Read source timer increment value
 * @hw: pointer to HW struct
 *
 * Read the increment value of the source timer and return it.
 */
u64 ice_ptp_read_src_incval(struct ice_hw *hw)
{
	u32 lo, hi;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);

	lo = rd32(hw, GLTSYN_INCVAL_L(tmr_idx));
	hi = rd32(hw, GLTSYN_INCVAL_H(tmr_idx));

	return ((u64)(hi & INCVAL_HIGH_M) << 32) | lo;
}

/* E822 family functions
 *
 * The following functions operate on the E822 family of devices.
 */

/**
 * ice_fill_phy_msg_e822 - Fill message data for a PHY register access
 * @msg: the PHY message buffer to fill in
 * @port: the port to access
 * @offset: the register offset
 */
static void
ice_fill_phy_msg_e822(struct ice_sbq_msg_input *msg, u8 port, u16 offset)
{
	int phy_port, phy, quadtype;

	phy_port = port % ICE_PORTS_PER_PHY;
	phy = port / ICE_PORTS_PER_PHY;
	quadtype = (port / ICE_PORTS_PER_QUAD) % ICE_NUM_QUAD_TYPE;

	if (quadtype == 0) {
		msg->msg_addr_low = P_Q0_L(P_0_BASE + offset, phy_port);
		msg->msg_addr_high = P_Q0_H(P_0_BASE + offset, phy_port);
	} else {
		msg->msg_addr_low = P_Q1_L(P_4_BASE + offset, phy_port);
		msg->msg_addr_high = P_Q1_H(P_4_BASE + offset, phy_port);
	}

	if (phy == 0)
		msg->dest_dev = rmn_0;
	else if (phy == 1)
		msg->dest_dev = rmn_1;
	else
		msg->dest_dev = rmn_2;
}

/**
 * ice_read_phy_reg_e822_lp - Read a PHY register
 * @hw: pointer to the HW struct
 * @port: PHY port to read from
 * @offset: PHY register offset to read
 * @val: on return, the contents read from the PHY
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Read a PHY register for the given port over the device sideband queue.
 */
static enum ice_status
ice_read_phy_reg_e822_lp(struct ice_hw *hw, u8 port, u16 offset, u32 *val,
			 bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;


	ice_fill_phy_msg_e822(&msg, port, offset);
	msg.opcode = ice_sbq_msg_rd;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	*val = msg.data;

	return 0;
}

enum ice_status
ice_read_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 *val)
{
	return ice_read_phy_reg_e822_lp(hw, port, offset, val, true);
}

/**
 * ice_write_phy_reg_e822_lp - Write a PHY register
 * @hw: pointer to the HW struct
 * @port: PHY port to write to
 * @offset: PHY register offset to write
 * @val: The value to write to the register
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Write a PHY register for the given port over the device sideband queue.
 */
static enum ice_status
ice_write_phy_reg_e822_lp(struct ice_hw *hw, u8 port, u16 offset, u32 val,
			  bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;


	ice_fill_phy_msg_e822(&msg, port, offset);
	msg.opcode = ice_sbq_msg_wr;
	msg.data = val;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	return 0;
}

enum ice_status
ice_write_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 val)
{
	return ice_write_phy_reg_e822_lp(hw, port, offset, val, true);
}

/**
 * ice_fill_quad_msg_e822 - Fill message data for quad register access
 * @msg: the PHY message buffer to fill in
 * @quad: the quad to access
 * @offset: the register offset
 *
 * Fill a message buffer for accessing a register in a quad shared between
 * multiple PHYs.
 */
static void
ice_fill_quad_msg_e822(struct ice_sbq_msg_input *msg, u8 quad, u16 offset)
{
	u32 addr;

	msg->dest_dev = rmn_0;

	if ((quad % ICE_NUM_QUAD_TYPE) == 0)
		addr = Q_0_BASE + offset;
	else
		addr = Q_1_BASE + offset;

	msg->msg_addr_low = ICE_LO_WORD(addr);
	msg->msg_addr_high = ICE_HI_WORD(addr);
}

/**
 * ice_read_quad_reg_e822_lp - Read a PHY quad register
 * @hw: pointer to the HW struct
 * @quad: quad to read from
 * @offset: quad register offset to read
 * @val: on return, the contents read from the quad
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Read a quad register over the device sideband queue. Quad registers are
 * shared between multiple PHYs.
 */
static enum ice_status
ice_read_quad_reg_e822_lp(struct ice_hw *hw, u8 quad, u16 offset, u32 *val,
			  bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;

	if (quad >= ICE_MAX_QUAD)
		return ICE_ERR_PARAM;

	ice_fill_quad_msg_e822(&msg, quad, offset);
	msg.opcode = ice_sbq_msg_rd;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	*val = msg.data;

	return 0;
}

enum ice_status
ice_read_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 *val)
{
	return ice_read_quad_reg_e822_lp(hw, quad, offset, val, true);
}

/**
 * ice_write_quad_reg_e822_lp - Write a PHY quad register
 * @hw: pointer to the HW struct
 * @quad: quad to write to
 * @offset: quad register offset to write
 * @val: The value to write to the register
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Write a quad register over the device sideband queue. Quad registers are
 * shared between multiple PHYs.
 */
static enum ice_status
ice_write_quad_reg_e822_lp(struct ice_hw *hw, u8 quad, u16 offset, u32 val,
			   bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;

	if (quad >= ICE_MAX_QUAD)
		return ICE_ERR_PARAM;

	ice_fill_quad_msg_e822(&msg, quad, offset);
	msg.opcode = ice_sbq_msg_wr;
	msg.data = val;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	return 0;
}

enum ice_status
ice_write_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 val)
{
	return ice_write_quad_reg_e822_lp(hw, quad, offset, val, true);
}

/**
 * ice_read_phy_tstamp_e822 - Read a PHY timestamp out of the quad block
 * @hw: pointer to the HW struct
 * @quad: the quad to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the two associated registers in the
 * quad memory block that is shared between the internal PHYs of the E822
 * family of devices.
 */
static enum ice_status
ice_read_phy_tstamp_e822(struct ice_hw *hw, u8 quad, u8 idx, u64 *tstamp)
{
	enum ice_status status;
	u16 lo_addr, hi_addr;
	u32 lo, hi;

	lo_addr = (u16)TS_L(Q_REG_TX_MEMORY_BANK_START, idx);
	hi_addr = (u16)TS_H(Q_REG_TX_MEMORY_BANK_START, idx);

	status = ice_read_quad_reg_e822(hw, quad, lo_addr, &lo);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read low PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	status = ice_read_quad_reg_e822(hw, quad, hi_addr, &hi);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read high PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	/* For E822 based internal PHYs, the timestamp is reported with the
	 * lower 8 bits in the low register, and the upper 32 bits in the high
	 * register.
	 */
	*tstamp = ((u64)hi) << TS_PHY_HIGH_S | ((u64)lo & TS_PHY_LOW_M);

	return 0;
}

/**
 * ice_clear_phy_tstamp_e822 - Clear a timestamp from the quad block
 * @hw: pointer to the HW struct
 * @quad: the quad to read from
 * @idx: the timestamp index to reset
 *
 * Clear a timestamp, resetting its valid bit, from the PHY quad block that is
 * shared between the internal PHYs on the E822 devices.
 */
static enum ice_status
ice_clear_phy_tstamp_e822(struct ice_hw *hw, u8 quad, u8 idx)
{
	enum ice_status status;
	u16 lo_addr, hi_addr;

	lo_addr = (u16)TS_L(Q_REG_TX_MEMORY_BANK_START, idx);
	hi_addr = (u16)TS_H(Q_REG_TX_MEMORY_BANK_START, idx);

	status = ice_write_quad_reg_e822(hw, quad, lo_addr, 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear low PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	status = ice_write_quad_reg_e822(hw, quad, hi_addr, 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear high PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_time_e822 - Prepare PHY port with initial time
 * @hw: pointer to the HW struct
 * @time: Time to initialize the PHY port clocks to
 *
 * Program the PHY port registers with a new initial time value. The port
 * clock will be initialized once the driver issues an INIT_TIME sync
 * command. The time value is the upper 32 bits of the PHY timer, usually in
 * units of nominal nanoseconds.
 */
static enum ice_status
ice_ptp_prep_phy_time_e822(struct ice_hw *hw, u32 time)
{
	enum ice_status status;
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {

		/* Tx case */
		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_TX_TIMER_INC_PRE_L,
						   0, true);
		if (status)
			goto exit_err;

		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_TX_TIMER_INC_PRE_U,
						   time, true);
		if (status)
			goto exit_err;

		/* Rx case */
		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_RX_TIMER_INC_PRE_L,
						   0, true);
		if (status)
			goto exit_err;

		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_RX_TIMER_INC_PRE_U,
						   time, true);
		if (status)
			goto exit_err;
	}

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write init time for port %u, status %d\n",
		  port, status);

	return status;
}

/**
 * ice_ptp_prep_port_adj_e822 - Prepare a single port for time adjust
 * @hw: pointer to HW struct
 * @port: Port number to be programmed
 * @time: time in cycles to adjust the port Tx and Rx clocks
 * @lock_sbq: true to lock the sbq sq_lock (the usual case); false if the
 *            sq_lock has already been locked at a higher level
 *
 * Program the port for an atomic adjustment by writing the Tx and Rx timer
 * registers. The atomic adjustment won't be completed until the driver issues
 * an ADJ_TIME command.
 *
 * Note that time is not in units of nanoseconds. It is in clock time
 * including the lower sub-nanosecond portion of the port timer.
 *
 * Negative adjustments are supported using 2s complement arithmetic.
 */
enum ice_status
ice_ptp_prep_port_adj_e822(struct ice_hw *hw, u8 port, s64 time,
			   bool lock_sbq)
{
	enum ice_status status;
	u32 l_time, u_time;

	l_time = lower_32_bits(time);
	u_time = upper_32_bits(time);

	/* Tx case */
	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_TX_TIMER_INC_PRE_L,
					   l_time, lock_sbq);
	if (status)
		goto exit_err;

	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_TX_TIMER_INC_PRE_U,
					   u_time, lock_sbq);
	if (status)
		goto exit_err;

	/* Rx case */
	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_RX_TIMER_INC_PRE_L,
					   l_time, lock_sbq);
	if (status)
		goto exit_err;

	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_RX_TIMER_INC_PRE_U,
					   u_time, lock_sbq);
	if (status)
		goto exit_err;

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write time adjust for port %u, status %d\n",
		  port, status);
	return status;
}

/**
 * ice_ptp_prep_phy_adj_e822 - Prep PHY ports for a time adjustment
 * @hw: pointer to HW struct
 * @adj: adjustment in nanoseconds
 * @lock_sbq: true to lock the sbq sq_lock (the usual case); false if the
 *            sq_lock has already been locked at a higher level
 *
 * Prepare the PHY ports for an atomic time adjustment by programming the PHY
 * Tx and Rx port registers. The actual adjustment is completed by issuing an
 * ADJ_TIME or ADJ_TIME_AT_TIME sync command.
 */
static enum ice_status
ice_ptp_prep_phy_adj_e822(struct ice_hw *hw, s32 adj, bool lock_sbq)
{
	s64 cycles;
	u8 port;

	/* The port clock supports adjustment of the sub-nanosecond portion of
	 * the clock. We shift the provided adjustment in nanoseconds to
	 * calculate the appropriate adjustment to program into the PHY ports.
	 */
	if (adj > 0)
		cycles = (s64)adj << 32;
	else
		cycles = -(((s64)-adj) << 32);

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		enum ice_status status;


		status = ice_ptp_prep_port_adj_e822(hw, port, cycles,
						    lock_sbq);
		if (status)
			return status;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_incval_e822 - Prepare PHY ports for time adjustment
 * @hw: pointer to HW struct
 * @incval: new increment value to prepare
 *
 * Prepare each of the PHY ports for a new increment value by programming each
 * port's TIMETUS registers. The new increment value will be updated after
 * issuing an INIT_INCVAL command.
 */
static enum ice_status
ice_ptp_prep_phy_incval_e822(struct ice_hw *hw, u64 incval)
{
	enum ice_status status;
	u32 high, low;
	u8 port;

	/* The PHY registers for the increment value divide the lower 8 bits
	 * into the first low register, and the next 32 bits into the second
	 * high register.
	 */
	low = (u32)(incval & P_REG_TIMETUS_LOW_M);
	high = (u32)(incval >> P_REG_TIMETUS_HIGH_S);

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		status = ice_write_phy_reg_e822_lp(hw, port, P_REG_TIMETUS_L,
						   low, true);
		if (status)
			goto exit_err;

		status = ice_write_phy_reg_e822_lp(hw, port, P_REG_TIMETUS_U,
						   high, true);
		if (status)
			goto exit_err;
	}

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write incval for port %u, status %d\n",
		  port, status);

	return status;
}

/**
 * ice_ptp_read_phy_incval_e822 - Read a PHY port's current incval
 * @hw: pointer to the HW struct
 * @port: the port to read
 * @incval: on return, the time_clk_cyc incval for this port
 *
 * Read the time_clk_cyc increment value for a given PHY port.
 */
enum ice_status
ice_ptp_read_phy_incval_e822(struct ice_hw *hw, u8 port, u64 *incval)
{
	enum ice_status status;
	u32 high, low;

	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_TIMETUS_L,
					  &low, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TIMETUS_L, status %d\n",
			  status);
		return status;
	}

	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_TIMETUS_U,
					  &high, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TIMETUS_U, status %d\n",
			  status);
		return status;
	}

	*incval = high << P_REG_TIMETUS_HIGH_S | (low & P_REG_TIMETUS_LOW_M);
	ice_debug(hw, ICE_DBG_PTP, "read INCVAL = 0x%08x%08x\n", high, low);

	return 0;
}

/**
 * ice_ptp_prep_phy_adj_target_e822 - Prepare PHY for adjust at target time
 * @hw: pointer to HW struct
 * @target_time: target time to program
 *
 * Program the PHY port Tx and Rx TIMER_CNT_ADJ registers used for the
 * ADJ_TIME_AT_TIME command. This should be used in conjunction with
 * ice_ptp_prep_phy_adj_e822 to program an atomic adjustment that is
 * delayed until a specified target time.
 *
 * Note that a target time adjustment is not currently supported on E810
 * devices.
 */
static enum ice_status
ice_ptp_prep_phy_adj_target_e822(struct ice_hw *hw, u32 target_time)
{
	enum ice_status status;
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {

		/* Tx case */
		/* No sub-nanoseconds data */
		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_TX_TIMER_CNT_ADJ_L,
						   0, true);
		if (status)
			goto exit_err;

		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_TX_TIMER_CNT_ADJ_U,
						   target_time, true);
		if (status)
			goto exit_err;

		/* Rx case */
		/* No sub-nanoseconds data */
		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_RX_TIMER_CNT_ADJ_L,
						   0, true);
		if (status)
			goto exit_err;

		status = ice_write_phy_reg_e822_lp(hw, port,
						   P_REG_RX_TIMER_CNT_ADJ_U,
						   target_time, true);
		if (status)
			goto exit_err;
	}

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write target time for port %u, status %d\n",
		  port, status);

	return status;
}

/**
 * ice_ptp_read_port_capture - Read a port's local time capture
 * @hw: pointer to HW struct
 * @port: Port number to read
 * @tx_ts: on return, the Tx port time capture
 * @rx_ts: on return, the Rx port time capture
 *
 * Read the port's Tx and Rx local time capture values.
 *
 * Note this has no equivalent for the E810 devices.
 */
enum ice_status
ice_ptp_read_port_capture(struct ice_hw *hw, u8 port, u64 *tx_ts, u64 *rx_ts)
{
	enum ice_status status;
	u32 high, low;

	/* Tx case */
	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_TX_CAPTURE_L,
					  &low, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read REG_TX_CAPTURE_L, status %d\n",
			  status);
		return status;
	}

	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_TX_CAPTURE_U,
					  &high, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_CAPTURE_U, status %d\n",
			  status);
		return status;
	}

	*tx_ts = (u64)high << 32 | low;
	ice_debug(hw, ICE_DBG_PTP, "tx_init = 0x%016llx\n", *tx_ts);

	/* Rx case */
	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_RX_CAPTURE_L,
					  &low, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_CAPTURE_L, status %d\n",
			  status);
		return status;
	}

	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_RX_CAPTURE_U,
					  &high, true);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_CAPTURE_U, status %d\n",
			  status);
		return status;
	}

	*rx_ts = (u64)high << 32 | low;
	ice_debug(hw, ICE_DBG_PTP, "rx_init = 0x%016llx\n", *rx_ts);

	return 0;
}

/**
 * ice_ptp_one_port_cmd - Prepare a single PHY port for a timer command
 * @hw: pointer to HW struct
 * @port: Port to which cmd has to be sent
 * @cmd: Command to be sent to the port
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Prepare the requested port for an upcoming timer sync command.
 *
 * Note there is no equivalent of this operation on E810, as that device
 * always handles all external PHYs internally.
 */
enum ice_status
ice_ptp_one_port_cmd(struct ice_hw *hw, u8 port, enum ice_ptp_tmr_cmd cmd,
		     bool lock_sbq)
{
	enum ice_status status;
	u32 cmd_val, val;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	cmd_val = tmr_idx << SEL_PHY_SRC;
	switch (cmd) {
	case INIT_TIME:
		cmd_val |= PHY_CMD_INIT_TIME;
		break;
	case INIT_INCVAL:
		cmd_val |= PHY_CMD_INIT_INCVAL;
		break;
	case ADJ_TIME:
		cmd_val |= PHY_CMD_ADJ_TIME;
		break;
	case ADJ_TIME_AT_TIME:
		cmd_val |= PHY_CMD_ADJ_TIME_AT_TIME;
		break;
	case READ_TIME:
		cmd_val |= PHY_CMD_READ_TIME;
		break;
	default:
		dev_warn(ice_hw_to_dev(hw), "Unknown timer command %u\n", cmd);
		return ICE_ERR_PARAM;
	}

	/* Tx case */
	/* Read, modify, write */
	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_TX_TMR_CMD, &val,
					  lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_TMR_CMD, status %d\n",
			  status);
		return status;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK;
	val |= cmd_val;

	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_TX_TMR_CMD, val,
					   lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back TX_TMR_CMD, status %d\n",
			  status);
		return status;
	}

	/* Rx case */
	/* Read, modify, write */
	status = ice_read_phy_reg_e822_lp(hw, port, P_REG_RX_TMR_CMD, &val,
					  lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_TMR_CMD, status %d\n",
			  status);
		return status;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK;
	val |= cmd_val;

	status = ice_write_phy_reg_e822_lp(hw, port, P_REG_RX_TMR_CMD, val,
					   lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back RX_TMR_CMD, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_port_cmd_e822 - Prepare all ports for a timer command
 * @hw: pointer to the HW struct
 * @cmd: timer command to prepare
 * @lock_sbq: true if the sideband queue lock must  be acquired
 *
 * Prepare all ports connected to this device for an upcoming timer sync
 * command.
 */
static enum ice_status
ice_ptp_port_cmd_e822(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd,
		      bool lock_sbq)
{
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		enum ice_status status;


		status = ice_ptp_one_port_cmd(hw, port, cmd, lock_sbq);
		if (status)
			return status;
	}

	return 0;
}

/* E822 Vernier calibration functions
 *
 * The following functions are used as part of the vernier calibration of
 * a port. This calibration increases the precision of the timestamps on the
 * port.
 */

/**
 * ice_ptp_set_vernier_wl - Set the window length for vernier calibration
 * @hw: pointer to the HW struct
 *
 * Set the window length used for the vernier port calibration process.
 */
enum ice_status ice_ptp_set_vernier_wl(struct ice_hw *hw)
{
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		enum ice_status status;


		status = ice_write_phy_reg_e822_lp(hw, port, P_REG_WL,
						   PTP_VERNIER_WL, true);
		if (status) {
			ice_debug(hw, ICE_DBG_PTP, "Failed to set vernier window length for port %u, status %d\n",
				  port, status);
			return status;
		}
	}

	return 0;
}

/**
 * ice_phy_get_speed_and_fec_e822 - Get link speed and FEC based on serdes mode
 * @hw: pointer to HW struct
 * @port: the port to read from
 * @link_out: if non-NULL, holds link speed on success
 * @fec_out: if non-NULL, holds FEC algorithm on success
 *
 * Read the serdes data for the PHY port and extract the link speed and FEC
 * algorithm.
 */
enum ice_status
ice_phy_get_speed_and_fec_e822(struct ice_hw *hw, u8 port,
			       enum ice_ptp_link_spd *link_out,
			       enum ice_ptp_fec_mode *fec_out)
{
	enum ice_ptp_link_spd link;
	enum ice_ptp_fec_mode fec;
	enum ice_status status;
	u32 serdes;

	status = ice_read_phy_reg_e822(hw, port, P_REG_LINK_SPEED, &serdes);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read serdes info\n");
		return status;
	}

	/* Determine the FEC algorithm */
	fec = (enum ice_ptp_fec_mode)P_REG_LINK_SPEED_FEC_MODE(serdes);

	serdes &= P_REG_LINK_SPEED_SERDES_M;

	/* Determine the link speed */
	if (fec == ICE_PTP_FEC_MODE_RS_FEC) {
		switch (serdes) {
		case ICE_PTP_SERDES_25G:
			link = ICE_PTP_LNK_SPD_25G_RS;
			break;
		case ICE_PTP_SERDES_50G:
			link = ICE_PTP_LNK_SPD_50G_RS;
			break;
		case ICE_PTP_SERDES_100G:
			link = ICE_PTP_LNK_SPD_100G_RS;
			break;
		default:
			return ICE_ERR_OUT_OF_RANGE;
		}
	} else {
		switch (serdes) {
		case ICE_PTP_SERDES_1G:
			link = ICE_PTP_LNK_SPD_1G;
			break;
		case ICE_PTP_SERDES_10G:
			link = ICE_PTP_LNK_SPD_10G;
			break;
		case ICE_PTP_SERDES_25G:
			link = ICE_PTP_LNK_SPD_25G;
			break;
		case ICE_PTP_SERDES_40G:
			link = ICE_PTP_LNK_SPD_40G;
			break;
		case ICE_PTP_SERDES_50G:
			link = ICE_PTP_LNK_SPD_50G;
			break;
		default:
			return ICE_ERR_OUT_OF_RANGE;
		}
	}

	if (link_out)
		*link_out = link;
	if (fec_out)
		*fec_out = fec;

	return 0;
}

/**
 * ice_phy_cfg_lane_e822 - Configure PHY quad for single/multi-lane timestamp
 * @hw: pointer to HW struct
 * @port: to configure the quad for
 */
void ice_phy_cfg_lane_e822(struct ice_hw *hw, u8 port)
{
	enum ice_ptp_link_spd link_spd;
	enum ice_status status;
	int quad;
	u32 val;

	quad = port / ICE_PORTS_PER_QUAD;

	status = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, NULL);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to get PHY link speed, status %d\n",
			  status);
		return;
	}

	status = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG, &val);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_MEM_GLB_CFG, status %d\n",
			  status);
		return;
	}

	if (link_spd >= ICE_PTP_LNK_SPD_40G)
		val &= ~Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_M;
	else
		val |= Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_M;

	status = ice_write_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG, val);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back TX_MEM_GBL_CFG, status %d\n",
			  status);
		return;
	}
}

/* E810 functions
 *
 * The following functions operate on the E810 series devices which use
 * a separate external PHY.
 */

/**
 * ice_read_phy_reg_e810_lp - Read register from external PHY on E810
 * @hw: pointer to the HW struct
 * @addr: the address to read from
 * @val: On return, the value read from the PHY
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Read a register from the external PHY on the E810 device.
 */
static enum ice_status
ice_read_phy_reg_e810_lp(struct ice_hw *hw, u32 addr, u32 *val, bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;

	msg.msg_addr_low = ICE_LO_WORD(addr);
	msg.msg_addr_high = ICE_HI_WORD(addr);
	msg.opcode = ice_sbq_msg_rd;
	msg.dest_dev = rmn_0;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	*val = msg.data;

	return 0;
}

static enum ice_status
ice_read_phy_reg_e810(struct ice_hw *hw, u32 addr, u32 *val)
{
	return ice_read_phy_reg_e810_lp(hw, addr, val, true);
}

/**
 * ice_write_phy_reg_e810_lp - Write register on external PHY on E810
 * @hw: pointer to the HW struct
 * @addr: the address to writem to
 * @val: the value to write to the PHY
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Write a value to a register of the external PHY on the E810 device.
 */
static enum ice_status
ice_write_phy_reg_e810_lp(struct ice_hw *hw, u32 addr, u32 val, bool lock_sbq)
{
	struct ice_sbq_msg_input msg = {0};
	enum ice_status status;

	msg.msg_addr_low = ICE_LO_WORD(addr);
	msg.msg_addr_high = ICE_HI_WORD(addr);
	msg.opcode = ice_sbq_msg_wr;
	msg.dest_dev = rmn_0;
	msg.data = val;

	status = ice_sbq_rw_reg_lp(hw, &msg, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to phy, status %d\n",
			  status);
		return status;
	}

	return 0;
}

static enum ice_status
ice_write_phy_reg_e810(struct ice_hw *hw, u32 addr, u32 val)
{
	return ice_write_phy_reg_e810_lp(hw, addr, val, true);
}

/**
 * ice_read_phy_tstamp_e810 - Read a PHY timestamp out of the external PHY
 * @hw: pointer to the HW struct
 * @lport: the lport to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the timestamp block of the external PHY
 * on the E810 device.
 */
static enum ice_status
ice_read_phy_tstamp_e810(struct ice_hw *hw, u8 lport, u8 idx, u64 *tstamp)
{
	enum ice_status status;
	u32 lo_addr, hi_addr, lo, hi;

	lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);

	status = ice_read_phy_reg_e810(hw, lo_addr, &lo);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read low PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	status = ice_read_phy_reg_e810(hw, hi_addr, &hi);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read high PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	/* For E810 devices, the timestamp is reported with the lower 32 bits
	 * in the low register, and the upper 8 bits in the high register.
	 */
	*tstamp = ((u64)hi) << TS_HIGH_S | ((u64)lo & TS_LOW_M);

	return 0;
}

/**
 * ice_clear_phy_tstamp_e810 - Clear a timestamp from the external PHY
 * @hw: pointer to the HW struct
 * @lport: the lport to read from
 * @idx: the timestamp index to reset
 *
 * Clear a timestamp, resetting its valid bit, from the timestamp block of the
 * external PHY on the E810 device.
 */
static enum ice_status
ice_clear_phy_tstamp_e810(struct ice_hw *hw, u8 lport, u8 idx)
{
	enum ice_status status;
	u32 lo_addr, hi_addr;

	lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);

	status = ice_write_phy_reg_e810(hw, lo_addr, 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear low PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	status = ice_write_phy_reg_e810(hw, hi_addr, 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear high PTP timestamp register, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_init_phy_e810 - Enable PTP function on the external PHY
 * @hw: pointer to HW struct
 *
 * Enable the timesync PTP functionality for the external PHY connected to
 * this function.
 *
 * Note there is no equivalent function needed on E822 based devices.
 */
enum ice_status ice_ptp_init_phy_e810(struct ice_hw *hw)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_ENA(tmr_idx),
					GLTSYN_ENA_TSYN_ENA_M);
	if (status)
		ice_debug(hw, ICE_DBG_PTP, "PTP failed in ena_phy_time_syn %d\n",
			  status);

	return status;
}

/**
 * ice_ptp_prep_phy_time_e810 - Prepare PHY port with initial time
 * @hw: Board private structure
 * @time: Time to initialize the PHY port clock to
 *
 * Program the PHY port ETH_GLTSYN_SHTIME registers in preparation setting the
 * initial clock time. The time will not actually be programmed until the
 * driver issues an INIT_TIME command.
 *
 * The time value is the upper 32 bits of the PHY timer, usually in units of
 * nominal nanoseconds.
 */
static enum ice_status ice_ptp_prep_phy_time_e810(struct ice_hw *hw, u32 time)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_0(tmr_idx), 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write SHTIME_0, status %d\n",
			  status);
		return status;
	}

	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_L(tmr_idx), time);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write SHTIME_L, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_adj_e810 - Prep PHY port for a time adjustment
 * @hw: pointer to HW struct
 * @adj: adjustment value to program
 * @lock_sbq: true if the sideband queue luck must be acquired
 *
 * Prepare the PHY port for an atomic adjustment by programming the PHY
 * ETH_GLTSYN_SHADJ_L and ETH_GLTSYN_SHADJ_H registers. The actual adjustment
 * is completed by issuing an ADJ_TIME sync command.
 *
 * The adjustment value only contains the portion used for the upper 32bits of
 * the PHY timer, usually in units of nominal nanoseconds. Negative
 * adjustments are supported using 2s complement arithmetic.
 */
static enum ice_status
ice_ptp_prep_phy_adj_e810(struct ice_hw *hw, s32 adj, bool lock_sbq)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Adjustments are represented as signed 2's complement values in
	 * nanoseconds. Sub-nanosecond adjustment is not supported.
	 */
	status = ice_write_phy_reg_e810_lp(hw, ETH_GLTSYN_SHADJ_L(tmr_idx),
					   0, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write adj to PHY SHADJ_L, status %d\n",
			  status);
		return status;
	}

	status = ice_write_phy_reg_e810_lp(hw, ETH_GLTSYN_SHADJ_H(tmr_idx),
					   adj, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write adj to PHY SHADJ_H, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_incval_e810 - Prep PHY port increment value change
 * @hw: pointer to HW struct
 * @incval: The new 40bit increment value to prepare
 *
 * Prepare the PHY port for a new increment value by programming the PHY
 * ETH_GLTSYN_SHADJ_L and ETH_GLTSYN_SHADJ_H registers. The actual change is
 * completed by issuing an INIT_INCVAL command.
 */
static enum ice_status
ice_ptp_prep_phy_incval_e810(struct ice_hw *hw, u64 incval)
{
	enum ice_status status;
	u32 high, low;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	low = lower_32_bits(incval);
	high = upper_32_bits(incval);

	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_L(tmr_idx), low);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write incval to PHY SHADJ_L, status %d\n",
			  status);
		return status;
	}

	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_H(tmr_idx), high);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write incval PHY SHADJ_H, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_adj_target_e810 - Prepare PHY port with adjust target
 * @hw: Board private structure
 * @target_time: Time to trigger the clock adjustment at
 *
 * Program the PHY port ETH_GLTSYN_SHTIME registers in preparation for
 * a target time adjust, which will trigger an adjustment of the clock in the
 * future. The actual adjustment will occur the next time the PHY port timer
 * crosses over the provided value after the driver issues an ADJ_TIME_AT_TIME
 * command.
 *
 * The time value is the upper 32 bits of the PHY timer, usually in units of
 * nominal nanoseconds.
 */
static enum ice_status
ice_ptp_prep_phy_adj_target_e810(struct ice_hw *hw, u32 target_time)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_0(tmr_idx), 0);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write target time to SHTIME_0, status %d\n",
			  status);
		return status;
	}

	status = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_L(tmr_idx),
					target_time);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write target time to SHTIME_L, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/**
 * ice_ptp_port_cmd_e810 - Prepare all external PHYs for a timer command
 * @hw: pointer to HW struct
 * @cmd: Command to be sent to the port
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Prepare the external PHYs connected to this device for a timer sync
 * command.
 */
static enum ice_status
ice_ptp_port_cmd_e810(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd,
		      bool lock_sbq)
{
	enum ice_status status;
	u32 cmd_val, val;

	switch (cmd) {
	case INIT_TIME:
		cmd_val = GLTSYN_CMD_INIT_TIME;
		break;
	case INIT_INCVAL:
		cmd_val = GLTSYN_CMD_INIT_INCVAL;
		break;
	case ADJ_TIME:
		cmd_val = GLTSYN_CMD_ADJ_TIME;
		break;
	case ADJ_TIME_AT_TIME:
		cmd_val = GLTSYN_CMD_ADJ_INIT_TIME;
		break;
	case READ_TIME:
		cmd_val = GLTSYN_CMD_READ_TIME;
		break;
	default:
		dev_warn(ice_hw_to_dev(hw), "Unknown timer command %u\n", cmd);
		return ICE_ERR_PARAM;
	}

	/* Read, modify, write */
	status = ice_read_phy_reg_e810_lp(hw, ETH_GLTSYN_CMD, &val, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read GLTSYN_CMD, status %d\n",
			  status);
		return status;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK_E810;
	val |= cmd_val;

	status = ice_write_phy_reg_e810_lp(hw, ETH_GLTSYN_CMD, val, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back GLTSYN_CMD, status %d\n",
			  status);
		return status;
	}

	return 0;
}

/* Device agnostic functions
 *
 * The following functions implement shared behavior common to both E822 and
 * E810 devices, possibly calling a device specific implementation where
 * necessary.
 */

/**
 * ice_ptp_lock - Acquire PTP global semaphore register lock
 * @hw: pointer to the HW struct
 *
 * Acquire the global PTP hardware semaphore lock. Returns true if the lock
 * was acquired, false otherwise.
 *
 * The PFTSYN_SEM register sets the busy bit on read, returning the previous
 * value. If software sees the busy bit cleared, this means that this function
 * acquired the lock (and the busy bit is now set). If software sees the busy
 * bit set, it means that another function acquired the lock.
 *
 * Software must clear the busy bit with a write to release the lock for other
 * functions when done.
 */
bool ice_ptp_lock(struct ice_hw *hw)
{
	u32 hw_lock;
	int i;

#define MAX_TRIES 5

	for (i = 0; i < MAX_TRIES; i++) {
		hw_lock = rd32(hw, PFTSYN_SEM + (PFTSYN_SEM_BYTES * hw->pf_id));
		hw_lock = hw_lock & PFTSYN_SEM_BUSY_M;
		if (hw_lock) {
			/* Somebody is holding the lock */
			msleep(10);
			continue;
		} else {
			break;
		}
	}

	return !hw_lock;
}

/**
 * ice_ptp_unlock - Release PTP global semaphore register lock
 * @hw: pointer to the HW struct
 *
 * Release the global PTP hardware semaphore lock. This is done by writing to
 * the PFTSYN_SEM register.
 */
void ice_ptp_unlock(struct ice_hw *hw)
{
	wr32(hw, PFTSYN_SEM + (PFTSYN_SEM_BYTES * hw->pf_id), 0);
}

/**
 * ice_ptp_src_cmd - Prepare source timer for a timer command
 * @hw: pointer to HW structure
 * @cmd: Timer command
 *
 * Prepare the source timer for an upcoming timer sync command.
 */
void ice_ptp_src_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	u32 cmd_val;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	cmd_val = tmr_idx << SEL_CPK_SRC;

	switch (cmd) {
	case INIT_TIME:
		cmd_val |= GLTSYN_CMD_INIT_TIME;
		break;
	case INIT_INCVAL:
		cmd_val |= GLTSYN_CMD_INIT_INCVAL;
		break;
	case ADJ_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_TIME;
		break;
	case ADJ_TIME_AT_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_INIT_TIME;
		break;
	case READ_TIME:
		cmd_val |= GLTSYN_CMD_READ_TIME;
		break;
	default:
		dev_warn(ice_hw_to_dev(hw), "Unknown timer command %u\n", cmd);
		return;
	}

	wr32(hw, GLTSYN_CMD, cmd_val);
}

/**
 * ice_ptp_tmr_cmd - Prepare and trigger a timer sync command
 * @hw: pointer to HW struct
 * @cmd: the command to issue
 * @lock_sbq: true if the sideband queue lock must be acquired
 *
 * Prepare the source timer and PHY timers and then trigger the requested
 * command. This causes the shadow registers previously written in preparation
 * for the command to be synchronously applied to both the source and PHY
 * timers.
 */
static enum ice_status
ice_ptp_tmr_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd, bool lock_sbq)
{
	enum ice_status status;

	/* First, prepare the source timer */
	ice_ptp_src_cmd(hw, cmd);

	/* Next, prepare the ports */
	if (ice_is_e810(hw))
		status = ice_ptp_port_cmd_e810(hw, cmd, lock_sbq);
	else
		status = ice_ptp_port_cmd_e822(hw, cmd, lock_sbq);
	if (status) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to prepare PHY ports for timer command %u, status %d\n",
			  cmd, status);
		return status;
	}

	/* Write the sync command register to drive both source and PHY timer
	 * commands synchronously
	 */
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);

	return 0;
}

/**
 * ice_ptp_init_time - Initialize device time to provided value
 * @hw: pointer to HW struct
 * @time: 64bits of time (GLTSYN_TIME_L and GLTSYN_TIME_H)
 *
 * Initialize the device to the specified time provided. This requires a three
 * step process:
 *
 * 1) write the new init time to the source timer shadow registers
 * 2) write the new init time to the phy timer shadow registers
 * 3) issue an init_time timer command to synchronously switch both the source
 *    and port timers to the new init time value at the next clock cycle.
 */
enum ice_status ice_ptp_init_time(struct ice_hw *hw, u64 time)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Source timers */
	wr32(hw, GLTSYN_SHTIME_L(tmr_idx), lower_32_bits(time));
	wr32(hw, GLTSYN_SHTIME_H(tmr_idx), upper_32_bits(time));
	wr32(hw, GLTSYN_SHTIME_0(tmr_idx), 0);

	/* PHY Clks */
	/* Fill Rx and Tx ports and send msg to PHY */
	if (ice_is_e810(hw))
		status = ice_ptp_prep_phy_time_e810(hw, time & 0xFFFFFFFF);
	else
		status = ice_ptp_prep_phy_time_e822(hw, time & 0xFFFFFFFF);
	if (status)
		return status;

	return ice_ptp_tmr_cmd(hw, INIT_TIME, true);
}

/**
 * ice_ptp_write_incval - Program PHC with new increment value
 * @hw: pointer to HW struct
 * @incval: Source timer increment value per clock cycle
 *
 * Program the PHC with a new increment value. This requires a three-step
 * process:
 *
 * 1) Write the increment value to the source timer shadow registers
 * 2) Write the increment value to the PHY timer shadow registers
 * 3) Issue an INIT_INCVAL timer command to synchronously switch both the
 *    source and port timers to the new increment value at the next clock
 *    cycle.
 */
enum ice_status ice_ptp_write_incval(struct ice_hw *hw, u64 incval)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Shadow Adjust */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), lower_32_bits(incval));
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), upper_32_bits(incval));

	if (ice_is_e810(hw))
		status = ice_ptp_prep_phy_incval_e810(hw, incval);
	else
		status = ice_ptp_prep_phy_incval_e822(hw, incval);
	if (status)
		return status;

	return ice_ptp_tmr_cmd(hw, INIT_INCVAL, true);
}

/**
 * ice_ptp_write_incval_locked - Program new incval while holding semaphore
 * @hw: pointer to HW struct
 * @incval: Source timer increment value per clock cycle
 *
 * Program a new PHC incval while holding the PTP semaphore.
 */
enum ice_status ice_ptp_write_incval_locked(struct ice_hw *hw, u64 incval)
{
	enum ice_status status;

	if (!ice_ptp_lock(hw))
		return ICE_ERR_NOT_READY;

	status = ice_ptp_write_incval(hw, incval);

	ice_ptp_unlock(hw);

	return status;
}

/**
 * ice_ptp_adj_clock - Adjust PHC clock time atomically
 * @hw: pointer to HW struct
 * @adj: Adjustment in nanoseconds
 * @lock_sbq: true to lock the sbq sq_lock (the usual case); false if the
 *            sq_lock has already been locked at a higher level
 *
 * Perform an atomic adjustment of the PHC time by the specified number of
 * nanoseconds. This requires a three-step process:
 *
 * 1) Write the adjustment to the source timer shadow registers
 * 2) Write the adjustment to the PHY timer shadow registers
 * 3) Issue an ADJ_TIME timer command to synchronously apply the adjustment to
 *    both the source and port timers at the next clock cycle.
 */
enum ice_status ice_ptp_adj_clock(struct ice_hw *hw, s32 adj, bool lock_sbq)
{
	enum ice_status status;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Write the desired clock adjustment into the GLTSYN_SHADJ register.
	 * For an ADJ_TIME command, this set of registers represents the value
	 * to add to the clock time. It supports subtraction by interpreting
	 * the value as a 2's complement integer.
	 */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), 0);
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), adj);

	if (ice_is_e810(hw))
		status = ice_ptp_prep_phy_adj_e810(hw, adj, lock_sbq);
	else
		status = ice_ptp_prep_phy_adj_e822(hw, adj, lock_sbq);
	if (status)
		return status;

	return ice_ptp_tmr_cmd(hw, ADJ_TIME, lock_sbq);
}

/**
 * ice_ptp_adj_clock_at_time - Adjust PHC atomically at specified time
 * @hw: pointer to HW struct
 * @at_time: Time in nanoseconds at which to perform the adjustment
 * @adj: Adjustment in nanoseconds
 *
 * Perform an atomic adjustment to the PHC clock at the specified time. This
 * requires a five-step process:
 *
 * 1) Write the adjustment to the source timer shadow adjust registers
 * 2) Write the target time to the source timer shadow time registers
 * 3) Write the adjustment to the PHY timers shadow adjust registers
 * 4) Write the target time to the PHY timers shadow adjust registers
 * 5) Issue an ADJ_TIME_AT_TIME command to initiate the atomic adjustment.
 */
enum ice_status
ice_ptp_adj_clock_at_time(struct ice_hw *hw, u64 at_time, s32 adj)
{
	enum ice_status status;
	u32 time_lo, time_hi;
	u8 tmr_idx;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	time_lo = lower_32_bits(at_time);
	time_hi = upper_32_bits(at_time);

	/* Write the desired clock adjustment into the GLTSYN_SHADJ register.
	 * For an ADJ_TIME_AT_TIME command, this set of registers represents
	 * the value to add to the clock time. It supports subtraction by
	 * interpreting the value as a 2's complement integer.
	 */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), 0);
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), adj);

	/* Write the target time to trigger the adjustment for source clock */
	wr32(hw, GLTSYN_SHTIME_0(tmr_idx), 0);
	wr32(hw, GLTSYN_SHTIME_L(tmr_idx), time_lo);
	wr32(hw, GLTSYN_SHTIME_H(tmr_idx), time_hi);

	/* Prepare PHY port adjustments */
	if (ice_is_e810(hw))
		status = ice_ptp_prep_phy_adj_e810(hw, adj, true);
	else
		status = ice_ptp_prep_phy_adj_e822(hw, adj, true);
	if (status)
		return status;

	/* Set target time for each PHY port */
	if (ice_is_e810(hw))
		status = ice_ptp_prep_phy_adj_target_e810(hw, time_lo);
	else
		status = ice_ptp_prep_phy_adj_target_e822(hw, time_lo);
	if (status)
		return status;

	return ice_ptp_tmr_cmd(hw, ADJ_TIME_AT_TIME, true);
}

/**
 * ice_read_phy_tstamp - Read a PHY timestamp from the timestamo block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the timestamp block. For E822 devices,
 * the block is the quad to read from. For E810 devices, the block is the
 * logical port to read from.
 */
enum ice_status
ice_read_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx, u64 *tstamp)
{
	if (ice_is_e810(hw))
		return ice_read_phy_tstamp_e810(hw, block, idx, tstamp);
	else
		return ice_read_phy_tstamp_e822(hw, block, idx, tstamp);
}

/**
 * ice_clear_phy_tstamp - Clear a timestamp from the timestamp block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to reset
 *
 * Clear a timestamp, resetting its valid bit, from the timestamp block. For
 * E822 devices, the block is the quad to clear from. For E810 devices, the
 * block is the logical port to clear from.
 */
enum ice_status
ice_clear_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx)
{
	if (ice_is_e810(hw))
		return ice_clear_phy_tstamp_e810(hw, block, idx);
	else
		return ice_clear_phy_tstamp_e822(hw, block, idx);
}
