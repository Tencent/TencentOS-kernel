// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2013, Intel Corporation. */

#include "iavf.h"

/**
 * iavf_ptp_disable_tx_tstamp - Disable timestamping in Tx rings
 * @adapter: private adapter structure
 *
 * Disable timestamp capture for all Tx rings
 */
static void iavf_ptp_disable_tx_tstamp(struct iavf_adapter *adapter)
{
	unsigned int i;

	for (i = 0; i < adapter->num_active_queues; i++)
		adapter->tx_rings[i].flags &= ~IAVF_TXRX_FLAGS_HW_TSTAMP;
}

/**
 * iavf_ptp_enable_tx_tstamp - Enable timestamping in Tx rings
 * @adapter: private adapter structure
 *
 * Enable timestamp capture for all Tx rings
 */
static void iavf_ptp_enable_tx_tstamp(struct iavf_adapter *adapter)
{
	unsigned int i;

	for (i = 0; i < adapter->num_active_queues; i++)
		adapter->tx_rings[i].flags |= IAVF_TXRX_FLAGS_HW_TSTAMP;
}

/**
 * iavf_ptp_disable_rx_tstamp - Disable timestamping in Rx rings
 * @adapter: private adapter structure
 *
 * Disable timestamp reporting for all Rx rings.
 */
static void iavf_ptp_disable_rx_tstamp(struct iavf_adapter *adapter)
{
	unsigned int i;

	for (i = 0; i < adapter->num_active_queues; i++)
		adapter->rx_rings[i].flags &= ~IAVF_TXRX_FLAGS_HW_TSTAMP;
}

/**
 * iavf_ptp_enable_rx_tstamp - Enable timestamping in Rx rings
 * @adapter: private adapter structure
 *
 * Enable timestamp reporting for all Rx rings.
 */
static void iavf_ptp_enable_rx_tstamp(struct iavf_adapter *adapter)
{
	unsigned int i;

	for (i = 0; i < adapter->num_active_queues; i++)
		adapter->rx_rings[i].flags |= IAVF_TXRX_FLAGS_HW_TSTAMP;
}

/**
 * iavf_ptp_set_timestamp_mode - Set device timestamping mode
 * @adapter: private adapter structure
 * @config: timestamping configuration request
 *
 * Set the timestamping mode requested from the SIOCSHWTSTAMP ioctl.
 *
 * Note: this function always translates Rx timestamp requests for any packet
 * category into HWTSTAMP_FILTER_ALL.
 */
static int
iavf_ptp_set_timestamp_mode(struct iavf_adapter *adapter, struct hwtstamp_config *config)
{
	/* Reserved for future extensions. */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		iavf_ptp_disable_tx_tstamp(adapter);
		break;
	case HWTSTAMP_TX_ON:
		if (!(iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_TX_TSTAMP)))
			return -EOPNOTSUPP;
		iavf_ptp_enable_tx_tstamp(adapter);
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		iavf_ptp_disable_rx_tstamp(adapter);
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
		if (!(iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_RX_TSTAMP)))
			return -EOPNOTSUPP;
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		iavf_ptp_enable_rx_tstamp(adapter);
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

/**
 * iavf_ptp_get_ts_config - Get timestamping configuration for SIOCGHWTSTAMP
 * @adapter: private adapter structure
 * @ifr: the ioctl request structure
 *
 * Copy the current hardware timestamping configuration back to userspace.
 * Called in response to the SIOCGHWTSTAMP ioctl that queries a device's
 * current timestamp settings.
 */
int iavf_ptp_get_ts_config(struct iavf_adapter *adapter, struct ifreq *ifr)
{
	struct hwtstamp_config *config = &adapter->ptp.hwtstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ? -EFAULT : 0;
}

/**
 * iavf_ptp_set_ts_config - Set timestamping configuration from SIOCSHWTSTAMP
 * @adapter: private adapter structure
 * @ifr: the ioctl request structure
 *
 * Program the requested timestamping configuration from SIOCSHWTSTAMP ioctl
 * to the device.
 */
int iavf_ptp_set_ts_config(struct iavf_adapter *adapter, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = iavf_ptp_set_timestamp_mode(adapter, &config);
	if (err)
		return err;

	/* Save successful settings for future reference */
	adapter->ptp.hwtstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

/**
 * clock_to_adapter - Convert clock info pointer to adapter pointer
 * @ptp_info: PTP info structure
 *
 * Use container_of in order to extract a pointer to the iAVF adapter private
 * structure.
 */
static struct iavf_adapter *clock_to_adapter(struct ptp_clock_info *ptp_info)
{
	struct iavf_ptp *ptp_priv;

	ptp_priv = container_of(ptp_info, struct iavf_ptp, info);
	return container_of(ptp_priv, struct iavf_adapter, ptp);
}

/**
 * iavf_ptp_cap_supported - Check if a PTP capability is supported
 * @adapter: private adapter structure
 * @cap: the capability bitmask to check
 *
 * Return true if every capability set in cap is also set in the enabled
 * capabilities reported by the PF.
 */
bool iavf_ptp_cap_supported(struct iavf_adapter *adapter, u32 cap)
{
	if (!PTP_ALLOWED(adapter))
		return false;

	/* Only return true if every bit in cap is set in hw_caps.caps */
	return (adapter->ptp.hw_caps.caps & cap) == cap;
}

/**
 * iavf_allocate_ptp_cmd - Allocate a PTP command message structure
 * @v_opcode: the virtchnl opcode
 * @msglen: length in bytes of the associated virtchnl structure
 *
 * Allocates a PTP command message and pre-fills it with the provided message
 * length and opcode.
 */
static struct iavf_ptp_aq_cmd *iavf_allocate_ptp_cmd(enum virtchnl_ops v_opcode, u16 msglen)
{
	struct iavf_ptp_aq_cmd *cmd;

	cmd = kzalloc(struct_size(cmd, msg, msglen), GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->v_opcode = v_opcode;
	cmd->msglen = msglen;

	return cmd;
}

/**
 * iavf_queue_ptp_cmd - Queue PTP command for sending over virtchnl
 * @adapter: private adapter structure
 * @cmd: the command structure to send
 *
 * Queue the given command structure into the PTP virtchnl command queue tos
 * end to the PF.
 */
static void iavf_queue_ptp_cmd(struct iavf_adapter *adapter, struct iavf_ptp_aq_cmd *cmd)
{
	spin_lock(&adapter->ptp.aq_cmd_lock);
	list_add_tail(&cmd->list, &adapter->ptp.aq_cmds);
	spin_unlock(&adapter->ptp.aq_cmd_lock);

	adapter->aq_required |= IAVF_FLAG_AQ_SEND_PTP_CMD;
	mod_delayed_work(iavf_wq, &adapter->watchdog_task, 0);
}

/**
 * iavf_send_phc_read - Send request to read PHC time
 * @adapter: private adapter structure
 *
 * Send a request to obtain the PTP hardware clock time. This allocates the
 * VIRTCHNL_OP_1588_PTP_GET_TIME message and queues it up to send to
 * indirectly read the PHC time.
 *
 * This function does not wait for the reply from the PF.
 */
static int iavf_send_phc_read(struct iavf_adapter *adapter)
{
	struct iavf_ptp_aq_cmd *cmd;

	if (!adapter->ptp.initialized)
		return -EOPNOTSUPP;

	cmd = iavf_allocate_ptp_cmd(VIRTCHNL_OP_1588_PTP_GET_TIME,
				    sizeof(struct virtchnl_phc_time));
	if (!cmd)
		return -ENOMEM;

	iavf_queue_ptp_cmd(adapter, cmd);

	return 0;
}

/**
 * iavf_read_phc_indirect - Indirectly read the PHC time via virtchnl
 * @adapter: private adapter structure
 * @ts: storage for the timestamp value
 * @sts: system timestamp values before and after the read
 *
 * Used when the device does not have direct register access to the PHC time.
 * Indirectly reads the time via the VIRTCHNL_OP_1588_PTP_GET_TIME, and waits
 * for the reply from the PF.
 *
 * Based on some simple measurements using ftrace and phc2sys, this clock
 * access method has about a ~110 usec latency even when the system is not
 * under load. In order to achieve acceptable results when using phc2sys with
 * the indirect clock access method, it is recommended to use more
 * conservative proportional and integration constants with the P/I servo.
 */
static int iavf_read_phc_indirect(struct iavf_adapter *adapter, struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	long ret;
	int err;

	adapter->ptp.phc_time_ready = false;
	ptp_read_system_prets(sts);

	err = iavf_send_phc_read(adapter);
	if (err)
		return err;

	ret = wait_event_interruptible_timeout(adapter->ptp.phc_time_waitqueue,
					       adapter->ptp.phc_time_ready,
					       HZ);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;

	*ts = ns_to_timespec64(adapter->ptp.cached_phc_time);

	ptp_read_system_postts(sts);

	return 0;
}

/**
 * iavf_read_phc_ns - Read PHC time from registers and convert to nanoseconds
 * @adapter: private adapter structure
 * @sts: system timestamp values before and after the read
 *
 * Capture the PHC time from the registers and convert it to nanoseconds.
 * Capture the system time before and after reading the lower clock register,
 * to allow more precise comparison between the PHC time and CLOCK_REALTIME.
 *
 * This requires direct access to the PHC registers, which may not be
 * available on all devices.
 *
 * If this method is available, it has a significantly reduced latency of
 * about 2 microseconds. It is preferred whenever available.
 */
static u64 iavf_read_phc_ns(struct iavf_adapter *adapter, struct ptp_system_timestamp *sts)
{
	u8 __iomem *phc_addr, *clock_lo, *clock_hi;
	u32 hi, hi2, lo;

	phc_addr = READ_ONCE(adapter->ptp.phc_addr);
	if (WARN_ON(!phc_addr))
		return 0;

	clock_lo = phc_addr + adapter->ptp.hw_caps.phc_regs.clock_lo;
	clock_hi = phc_addr + adapter->ptp.hw_caps.phc_regs.clock_hi;

	hi = readl(clock_hi);
	ptp_read_system_prets(sts);
	lo = readl(clock_lo);
	ptp_read_system_postts(sts);
	hi2 = readl(clock_hi);

	if (hi != hi2) {
		/* clock_lo might have rolled over, so recapture it */
		ptp_read_system_prets(sts);
		lo = readl(clock_lo);
		ptp_read_system_postts(sts);
		hi = hi2;
	}

	return ((u64)hi << 32) | lo;
}

/**
 * iavf_read_phc_direct - Directly read PHC time from the registers
 * @adapter: private adapter structure
 * @ts: storage for the PHC time
 * @sts: system timestamp values before and after the read
 *
 * Read the PHC time from the registers, and convert it to a timespec64.
 */
static int iavf_read_phc_direct(struct iavf_adapter *adapter, struct timespec64 *ts,
				struct ptp_system_timestamp *sts)
{
	u64 time = iavf_read_phc_ns(adapter, sts);

	*ts = ns_to_timespec64(time);

	return 0;
}

/**
 * iavf_ptp_gettimex64 - Get current PTP clock time
 * @ptp: PTP clock info structure
 * @ts: storage for the current time
 * @sts: system timestamps before and after time captured
 *
 * Read the current PTP clock time, and return it in the ts structure. Capture
 * the system time before and after the PTP clock time in sts. Note that
 * ptp_read_sytsem_prets and ptp_read_system_postts are NULL-aware and will do
 * nothing if sts is NULL.
 */
static int iavf_ptp_gettimex64(struct ptp_clock_info *ptp, struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct iavf_adapter *adapter = clock_to_adapter(ptp);

	if (!adapter->ptp.initialized)
		return -ENODEV;

	if (adapter->ptp.phc_addr)
		return iavf_read_phc_direct(adapter, ts, sts);
	else
		return iavf_read_phc_indirect(adapter, ts, sts);
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIMEX64
/**
 * iavf_ptp_gettime64 - wrapper in case ptp_caps doesn't have .gettimex64
 * @ptp: PTP clock info structure
 * @ts: storage for the current time
 *
 * Implement .gettime64 for the PTP clock. Wrapper that just calls
 * iavf_ptp_gettimex64 with a NULL sts pointer.
 */
static int iavf_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	return iavf_ptp_gettimex64(ptp, ts, NULL);
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIME64
/**
 * iavf_ptp_gettime32 - wrapper in case ptp_caps doesn't have .gettime64
 * @ptp: PTP clock info structure
 * @ts: storage for the current time
 *
 * Implement .gettime for the PTP clock. Wrapper that just calls
 * iavf_ptp_gettime64 and converts the timespec back to a 32bit timespec
 * before returning.
 */
static int iavf_ptp_gettime32(struct ptp_clock_info *ptp, struct timespec *ts)
{
	struct timespec64 ts64;
	int err;

	err = iavf_ptp_gettime64(ptp, &ts64);
	if (err)
		return err;

	*ts = timespec64_to_timespec(ts64);
	return 0;
}
#endif /* !HAVE_PTP_CLOCK_INFO_GETTIME64 */
#endif /* !HAVE_PTP_CLOCK_INFO_GETTIMEX64 */

/**
 * iavf_ptp_settime64 - Set PTP clock time
 * @ptp: PTP clock info structure
 * @ts: the time to set the clock to
 *
 * Set the PTP clock time to the requested value.
 */
static int iavf_ptp_settime64(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct iavf_adapter *adapter = clock_to_adapter(ptp);
	struct virtchnl_phc_time *msg;
	struct iavf_ptp_aq_cmd *cmd;

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_WRITE_PHC))
		return -EACCES;

	if (!adapter->ptp.initialized)
		return -ENODEV;

	cmd = iavf_allocate_ptp_cmd(VIRTCHNL_OP_1588_PTP_SET_TIME, sizeof(*msg));
	if (!cmd)
		return -ENOMEM;

	msg = (typeof(msg))cmd->msg;
	msg->time = timespec64_to_ns(ts);

	iavf_queue_ptp_cmd(adapter, cmd);

	return 0;
}

#ifndef HAVE_PTP_CLOCK_INFO_GETTIME64
/**
 * iavf_ptp_settime32 - wrapper in case ptp_caps doesn't have .settime64
 * @ptp: PTP clock info structure
 * @ts: 32bit timespec with requested time
 *
 * Implement .settime for the PTP clock. Wrapper that just calls
 * iavf_ptp_settime64 after converting the 32bit timespec to a 64bit timespec.
 */
static int iavf_ptp_settime32(struct ptp_clock_info *ptp, const struct timespec *ts)
{
	struct timespec64 ts64 = timespec_to_timespec64(*ts);

	return iavf_ptp_settime64(ptp, &ts64);
}
#endif

/**
 * iavf_ptp_adjtime - Adjust PTP clock time by requested amount
 * @ptp: PTP clock info structure
 * @delta: Offset in nanoseconds to adjust the clock time by
 *
 * Adjust the PTP clock time by the provided delta.
 */
static int iavf_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct iavf_adapter *adapter = clock_to_adapter(ptp);
	struct virtchnl_phc_adj_time *msg;
	struct iavf_ptp_aq_cmd *cmd;

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_WRITE_PHC))
		return -EACCES;

	if (!adapter->ptp.initialized)
		return -ENODEV;

	cmd = iavf_allocate_ptp_cmd(VIRTCHNL_OP_1588_PTP_ADJ_TIME, sizeof(*msg));
	if (!cmd)
		return -ENOMEM;

	msg = (typeof(msg))cmd->msg;
	msg->delta = delta;

	iavf_queue_ptp_cmd(adapter, cmd);

	return 0;
}

/**
 * iavf_ptp_adjfine - Adjust PTP clock time by scaled parts per million
 * @ptp: PTP clock info structure
 * @scaled_ppm: scaled parts per million adjustment
 *
 * Perform a frequency adjustment by the provided scaled parts per million
 * value.
 */
static int iavf_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct iavf_adapter *adapter = clock_to_adapter(ptp);
	struct virtchnl_phc_adj_freq *msg;
	struct iavf_ptp_aq_cmd *cmd;

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_WRITE_PHC))
		return -EACCES;

	if (!adapter->ptp.initialized)
		return -ENODEV;

	cmd = iavf_allocate_ptp_cmd(VIRTCHNL_OP_1588_PTP_ADJ_FREQ, sizeof(*msg));
	if (!cmd)
		return -ENOMEM;

	msg = (typeof(msg))cmd->msg;
	msg->scaled_ppm = (s64)scaled_ppm;

	iavf_queue_ptp_cmd(adapter, cmd);

	return 0;
}

#ifndef HAVE_PTP_CLOCK_INFO_ADJFINE
/**
 * ppb_to_scaled_ppm - Convert parts per billion to scaled parts per million
 * @ppb: parts per billion value
 *
 * Older versions of the kernel stack request frequency adjustments in parts
 * per billion. Newer kernels can request adjustment using the full 'freq'
 * field from the 'struct timex'. This is represented as parts per million,
 * but with a 16 bit binary fractional field, i.e. parts per 1 million * 2^16.
 *
 * In essence, this is adjustments in parts per 65,536,000,000, which we call
 * scaled_ppm.
 *
 * The following equation shows the relationship between ppb and scaled_ppm:
 *
 *   ppb = scaled_ppm * 1000 / 2^16
 *
 * i.e.
 *
 *   scaled_ppm = (ppb / 1000) * 2^16
 *
 * We can further simplify this to:
 *
 *   scaled_ppm = ( ppb / 125 ) * 2^13
 *
 * For reference, here is the approximate conversion between scaled_ppm and ppb:
 *
 *   1 scaled_ppm ~= 0.015 ppb
 *   1 ppb ~= 65.5 scaled_ppm
 */
static long ppb_to_scaled_ppm(s32 ppb)
{
	long scaled_ppm;

	scaled_ppm = (s64)ppb << 13;
	scaled_ppm /= 125;

	return scaled_ppm;
}

/**
 * iavf_ptp_adjfreq - wrapper in case ptp_caps doesn't have .adjfine
 * @ptp: PTP clock info structure
 * @ppb: parts per billion frequency adjustment
 *
 * Implement .adjfreq for the PTP clock. Wrapper that converts ppb to
 * scaled_ppm and then calls iavf_ptp_adjfine.
 */
static int iavf_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	return iavf_ptp_adjfine(ptp, ppb_to_scaled_ppm(ppb));
}
#endif

/**
 * iavf_ptp_tx_hang - Detect when Tx timestamp has taken too long
 * @adapter: private adapter structure
 *
 * Detect when a Tx timestamp event has been outstanding for more than one
 * second. If this occurs, discard the waiting SKB and clear the flag.
 *
 * This is important for two reasons. First, if a timestamp event is missed
 * and we do nothing, the driver could prevent all future timestamp requests
 * indefinitely. Second, if a timestamp event is late, the timestamp extension
 * algorithm might incorrectly calculate the wrong timestamp.
 */
static void iavf_ptp_tx_hang(struct iavf_adapter *adapter)
{
	if (!test_bit(__IAVF_TX_TSTAMP_IN_PROGRESS, &adapter->crit_section))
		return;

	if (time_is_before_jiffies(adapter->ptp.tx_start + HZ)) {
		struct sk_buff *skb = adapter->ptp.tx_skb;

		adapter->ptp.tx_skb = NULL;
		clear_bit_unlock(__IAVF_TX_TSTAMP_IN_PROGRESS, &adapter->crit_section);

		/* Free the SKB after we've cleared the bitlock */
		dev_kfree_skb_any(skb);
		adapter->ptp.tx_hwtstamp_timeouts++;
	}
}

/**
 * iavf_ptp_cache_phc_time - Cache PHC time for performing timestamp extension
 * @adapter: private adapter structure
 *
 * Periodically cache the PHC time in order to allow for timestamp extension.
 * This is required because the Tx and Rx timestamps only contain 32bits of
 * nanoseconds. Timestamp extension allows calculating the corrected 64bit
 * timestamp. This algorithm relies on the cached time being within ~1 second
 * of the timestamp.
 */
static void iavf_ptp_cache_phc_time(struct iavf_adapter *adapter)
{
	if (time_is_before_jiffies(adapter->ptp.cached_phc_updated + HZ)) {
		if (adapter->ptp.phc_addr) {
			adapter->ptp.cached_phc_time = iavf_read_phc_ns(adapter, NULL);
			adapter->ptp.cached_phc_updated = jiffies;
		} else {
			/* The response from virtchnl will store the time into cached_phc_time */
			iavf_send_phc_read(adapter);
		}
	}
}

/**
 * iavf_ptp_do_aux_work - Perform periodic work required for PTP support
 * @ptp: PTP clock info structure
 *
 * Handler to take care of periodic work required for PTP operation. This
 * includes the following tasks:
 *
 *   1) updating cached_phc_time
 *
 *      cached_phc_time is used by the Tx and Rx timestamp flows in order to
 *      perform timestamp extension, by carefully comparing the timestamp
 *      32bit nanosecond timestamps and determining the corrected 64bit
 *      timestamp value to report to userspace. This algorithm only works if
 *      the cached_phc_time is within ~1 second of the Tx or Rx timestamp
 *      event. This task periodically reads the PHC time and stores it, to
 *      ensure that timestamp extension operates correctly.
 *
 *   2) canceling outstanding Tx timestamp events
 *
 *      Tx timestamps require waiting to receive a timestamp event indication
 *      from hardware. In some rare cases, the packet might have been dropped
 *      without a timestamp. If this occurs, the Tx timestamp event will never
 *      complete. To avoid this, we check if a timestamp event has taken too
 *      long, and discard it if so.
 *
 * Returns: time in jiffies until the periodic task should be re-scheduled.
 */
long iavf_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct iavf_adapter *adapter = clock_to_adapter(ptp);

	iavf_ptp_cache_phc_time(adapter);
	iavf_ptp_tx_hang(adapter);

	/* Check work about twice a second */
	return msecs_to_jiffies(500);
}

/**
 * iavf_ptp_register_clock - Register a new PTP for userspace
 * @adapter: private adapter structure
 *
 * Allocate and register a new PTP clock device if necessary.
 */
static int iavf_ptp_register_clock(struct iavf_adapter *adapter)
{
	struct ptp_clock_info *ptp_info = &adapter->ptp.info;
	struct device *dev = &adapter->pdev->dev;

	memset(ptp_info, 0, sizeof(*ptp_info));

	snprintf(ptp_info->name, sizeof(ptp_info->name) - 1, "%s-%s-clk", dev_driver_string(dev),
		 netdev_name(adapter->netdev));
	ptp_info->owner = THIS_MODULE;
	ptp_info->max_adj = adapter->ptp.hw_caps.max_adj;

#if defined(HAVE_PTP_CLOCK_INFO_GETTIMEX64)
	ptp_info->gettimex64 = iavf_ptp_gettimex64;
#elif defined(HAVE_PTP_CLOCK_INFO_GETTIME64)
	ptp_info->gettime64 = iavf_ptp_gettime64;
#else
	ptp_info->gettime = iavf_ptp_gettime32;
#endif
#ifdef HAVE_PTP_CLOCK_INFO_GETTIME64
	ptp_info->settime64 = iavf_ptp_settime64;
#else
	ptp_info->settime = iavf_ptp_settime32;
#endif
	ptp_info->adjtime = iavf_ptp_adjtime;
#ifdef HAVE_PTP_CLOCK_INFO_ADJFINE
	ptp_info->adjfine = iavf_ptp_adjfine;
#else
	ptp_info->adjfreq = iavf_ptp_adjfreq;
#endif
#ifdef HAVE_PTP_CLOCK_DO_AUX_WORK
	ptp_info->do_aux_work = iavf_ptp_do_aux_work;
#endif

	dev_info(&adapter->pdev->dev, "registering PTP clock %s\n", adapter->ptp.info.name);

	adapter->ptp.clock = ptp_clock_register(ptp_info, dev);
	if (IS_ERR(adapter->ptp.clock))
		return PTR_ERR(adapter->ptp.clock);

	return 0;
}

/**
 * iavf_ptp_map_phc_addr - Map PHC clock register region
 * @adapter: private adapter structure
 *
 * Map the PCI region that contains the PTP hardware clock registers for
 * directly accessing the device time.
 */
static void iavf_ptp_map_phc_addr(struct iavf_adapter *adapter)
{
	struct virtchnl_ptp_caps *hw_caps = &adapter->ptp.hw_caps;
	struct device *dev = &adapter->pdev->dev;
	resource_size_t region_size;
	void __iomem *phc_addr;

	WARN(adapter->ptp.phc_addr, "PHC clock register address already mapped");

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_PHC_REGS)) {
		dev_dbg(dev, "Device does not have direct clock register access. Falling back to indirect clock access\n");
		return;
	}

	region_size = pci_resource_len(adapter->pdev, hw_caps->phc_regs.pcie_region);

	if (hw_caps->phc_regs.clock_lo > region_size) {
		dev_warn(dev, "Low clock register outside of PHC bar area. Falling back to indirect clock access\n");
		return;
	}

	if (hw_caps->phc_regs.clock_hi > region_size) {
		dev_warn(dev, "High clock register outside of PHC bar area. Falling back to indirect clock access\n");
		return;
	}

	phc_addr = pci_ioremap_bar(adapter->pdev, hw_caps->phc_regs.pcie_region);
	if (!phc_addr) {
		dev_warn(dev, "Unable to map PHC registers for clock access. Falling back to indirect clock access\n");
		return;
	}

	adapter->ptp.phc_addr = phc_addr;
}

/**
 * iavf_ptp_unmap_phc_addr - Unmap the PHC clock register region
 * @adapter: private adapter structure
 *
 * Unmap and release the PHC clock register region.
 */
static void iavf_ptp_unmap_phc_addr(struct iavf_adapter *adapter)
{
	if (adapter->ptp.phc_addr) {
		iounmap(adapter->ptp.phc_addr);
		adapter->ptp.phc_addr = NULL;
	}
}

/**
 * iavf_validate_tx_tstamp_format - Check if driver knows timestamp format
 * @adapter: private adapter structure
 *
 * Check that the driver understands the timestamp format that the PF
 * indicated. If we do not understand the format, then we must disable Tx
 * timestamps. Otherwise we might process timestamps from
 * VIRTCHNL_OP_1588_PTP_TX_TSTAMP incorrectly.
 */
static void iavf_validate_tx_tstamp_format(struct iavf_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	switch (adapter->ptp.hw_caps.tx_tstamp_format) {
	case VIRTCHNL_1588_PTP_TSTAMP_40BIT:
	case VIRTCHNL_1588_PTP_TSTAMP_64BIT_NS:
		dev_dbg(dev, "%s: got Tx timestamp format %u\n",
			__func__, adapter->ptp.hw_caps.tx_tstamp_format);
		break;
	default:
		dev_warn(dev, "Disabling Tx timestamps due to unexpected Tx timestamp format %u\n",
			 adapter->ptp.hw_caps.tx_tstamp_format);
		adapter->ptp.hw_caps.caps &= ~VIRTCHNL_1588_PTP_CAP_TX_TSTAMP;
		break;
	}
}

/**
 * iavf_ptp_init - Initialize PTP support if capability was negotiated
 * @adapter: private adapter structure
 *
 * Initialize PTP functionality, based on the capabilities that the PF has
 * enabled for this VF.
 */
void iavf_ptp_init(struct iavf_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	int err;

	if (WARN_ON(adapter->ptp.initialized)) {
		dev_err(dev, "PTP functionality was already initialized!\n");
		return;
	}

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_READ_PHC)) {
		dev_dbg(dev, "Device does not have PTP clock support\n");
		return;
	}

	err = iavf_ptp_register_clock(adapter);
	if (err) {
		dev_warn(dev, "Failed to register PTP clock device\n");
		return;
	}

#ifdef HAVE_PTP_CLOCK_DO_AUX_WORK
	ptp_schedule_worker(adapter->ptp.clock, 0);
#endif

	iavf_ptp_map_phc_addr(adapter);

	iavf_validate_tx_tstamp_format(adapter);

	adapter->ptp.initialized = true;
}

/**
 * iavf_ptp_release - Disable PTP support
 * @adapter: private adapter structure
 *
 * Release all PTP resources that were previously initialized.
 */
void iavf_ptp_release(struct iavf_adapter *adapter)
{
	struct iavf_ptp_aq_cmd *cmd, *tmp;

	if (!IS_ERR_OR_NULL(adapter->ptp.clock)) {
		dev_info(&adapter->pdev->dev, "removing PTP clock %s\n", adapter->ptp.info.name);
		ptp_clock_unregister(adapter->ptp.clock);
		adapter->ptp.clock = NULL;
	}

	/* Cancel any remaining uncompleted PTP clock commands */
	spin_lock(&adapter->ptp.aq_cmd_lock);
	list_for_each_entry_safe(cmd, tmp, &adapter->ptp.aq_cmds, list) {
		list_del(&cmd->list);
		kfree(cmd);
	}
	adapter->aq_required &= ~IAVF_FLAG_AQ_SEND_PTP_CMD;
	spin_unlock(&adapter->ptp.aq_cmd_lock);

	iavf_ptp_unmap_phc_addr(adapter);

	adapter->ptp.hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;
	iavf_ptp_disable_tx_tstamp(adapter);

	adapter->ptp.hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	iavf_ptp_disable_rx_tstamp(adapter);

	adapter->ptp.initialized = false;
}

/**
 * iavf_ptp_process_caps - Handle change in PTP capabilities
 * @adapter: private adapter structure
 *
 * Handle any state changes necessary due to change in PTP capabilities, such
 * as after a device reset or change in configuration from the PF.
 */
void iavf_ptp_process_caps(struct iavf_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	dev_dbg(dev, "PTP capabilities changed at runtime\n");

	/* Check if we lost PTP capability after loading */
	if (adapter->ptp.initialized &&
	    !iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_READ_PHC)) {
		iavf_ptp_release(adapter);
		return;
	}

	/* Check if we gained PTP capability after loading */
	if (!adapter->ptp.initialized &&
	    iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_READ_PHC)) {
		iavf_ptp_init(adapter);
		return;
	}

	/* The following checks are only necessary if we still have PTP clock
	 * capability. These handle if one of the extended capabilities is
	 * changed.
	 */

	if (adapter->ptp.phc_addr &&
	    !(iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_PHC_REGS)))
		iavf_ptp_unmap_phc_addr(adapter);
	else if (!adapter->ptp.phc_addr &&
		 (iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_PHC_REGS)))
		iavf_ptp_map_phc_addr(adapter);

	iavf_validate_tx_tstamp_format(adapter);

	/* Check if the device lost access to Tx timestamp outgoing packets */
	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_TX_TSTAMP)) {
		adapter->ptp.hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;
		iavf_ptp_disable_tx_tstamp(adapter);
	}

	/* Check if the device lost access to Rx timestamp incoming packets */
	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_RX_TSTAMP)) {
		adapter->ptp.hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
		iavf_ptp_disable_rx_tstamp(adapter);
	}
}

/**
 * iavf_ptp_extend_32b_timestamp - Convert a 32b nanoseconds timestamp to 64b nanoseconds
 * @cached_phc_time: recently cached copy of PHC time
 * @in_tstamp: Ingress/egress 32b nanoseconds timestamp value
 *
 * Hardware captures timestamps which contain only 32 bits of nominal
 * nanoseconds, as opposed to the 64bit timestamps that the stack expects.
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
u64 iavf_ptp_extend_32b_timestamp(u64 cached_phc_time, u32 in_tstamp)
{
	const u64 mask = GENMASK_ULL(31, 0);
	u32 delta;
	u64 ns;

	/* Calculate the delta between the lower 32bits of the cached PHC
	 * time and the in_tstamp value
	 */
	delta = (in_tstamp - (u32)(cached_phc_time & mask));

	/* Do not assume that the in_tstamp is always more recent than the
	 * cached PHC time. If the delta is large, it indicates that the
	 * in_tstamp was taken in the past, and should be converted
	 * forward.
	 */
	if (delta > (mask / 2)) {
		/* reverse the delta calculation here */
		delta = ((u32)(cached_phc_time & mask) - in_tstamp);
		ns = cached_phc_time - delta;
	} else {
		ns = cached_phc_time + delta;
	}

	return ns;
}

/**
 * iavf_ptp_extend_40b_timestamp - Convert a 40b timestamp to 64b nanoseconds
 * @cached_phc_time: recently cached copy of PHC time
 * @in_tstamp: Ingress/egress 40b timestamp value
 *
 * For some devices, the Tx and Rx timestamps use a 40bit timestamp:
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
 * Extract the 32bit nominal nanoseconds and extend them. See
 * iavf_ptp_extend_32b_timestamp for a detailed explanation of the extension
 * algorithm.
 */
u64 iavf_ptp_extend_40b_timestamp(u64 cached_phc_time, u64 in_tstamp)
{
	const u64 mask = GENMASK_ULL(31, 0);

	return iavf_ptp_extend_32b_timestamp(cached_phc_time, (in_tstamp >> 8) & mask);
}
