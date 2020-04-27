/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_PTP_H
#define BNXT_PTP_H

#define BNXT_MAX_PHC_DRIFT	31000000

struct bnxt_ptp_cfg {
#ifdef HAVE_IEEE1588_SUPPORT
	struct ptp_clock_info	ptp_info;
	struct ptp_clock	*ptp_clock;
	struct cyclecounter	cc;
	struct timecounter	tc;
#endif
	struct bnxt		*bp;
	atomic_t		tx_avail;
#define BNXT_MAX_TX_TS	1
	u16			rxctl;
#define BNXT_PTP_MSG_SYNC			(1 << 0)
#define BNXT_PTP_MSG_DELAY_REQ			(1 << 1)
#define BNXT_PTP_MSG_PDELAY_REQ			(1 << 2)
#define BNXT_PTP_MSG_PDELAY_RESP		(1 << 3)
#define BNXT_PTP_MSG_FOLLOW_UP			(1 << 8)
#define BNXT_PTP_MSG_DELAY_RESP			(1 << 9)
#define BNXT_PTP_MSG_PDELAY_RESP_FOLLOW_UP	(1 << 10)
#define BNXT_PTP_MSG_ANNOUNCE			(1 << 11)
#define BNXT_PTP_MSG_SIGNALING			(1 << 12)
#define BNXT_PTP_MSG_MANAGEMENT			(1 << 13)
#define BNXT_PTP_MSG_EVENTS		(BNXT_PTP_MSG_SYNC |		\
					 BNXT_PTP_MSG_DELAY_REQ |	\
					 BNXT_PTP_MSG_PDELAY_REQ |	\
					 BNXT_PTP_MSG_PDELAY_RESP)
	u8			tx_tstamp_en:1;
	int			rx_filter;

#define BNXT_PTP_RX_TS_L	0
#define BNXT_PTP_RX_TS_H	1
#define BNXT_PTP_RX_SEQ		2
#define BNXT_PTP_RX_FIFO	3
#define BNXT_PTP_RX_FIFO_PENDING 0x1
#define BNXT_PTP_RX_FIFO_ADV	4
#define BNXT_PTP_RX_REGS	5

#define BNXT_PTP_TX_TS_L	0
#define BNXT_PTP_TX_TS_H	1
#define BNXT_PTP_TX_SEQ		2
#define BNXT_PTP_TX_FIFO	3
#define BNXT_PTP_TX_FIFO_EMPTY	 0x2
#define BNXT_PTP_TX_REGS	4
	u32			rx_regs[BNXT_PTP_RX_REGS];
	u32			rx_mapped_regs[BNXT_PTP_RX_REGS];
	u32			tx_regs[BNXT_PTP_TX_REGS];
	u32			tx_mapped_regs[BNXT_PTP_TX_REGS];
};

int bnxt_hwtstamp_set(struct net_device *dev, struct ifreq *ifr);
int bnxt_hwtstamp_get(struct net_device *dev, struct ifreq *ifr);
int bnxt_get_tx_ts(struct bnxt *bp, u64 *ts);
int bnxt_get_rx_ts(struct bnxt *bp, u64 *ts);
int bnxt_ptp_init(struct bnxt *bp);
void bnxt_ptp_free(struct bnxt *bp);

#endif
