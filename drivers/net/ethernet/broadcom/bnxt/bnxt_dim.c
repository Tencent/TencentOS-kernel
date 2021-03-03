/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include "bnxt_compat.h"
#ifdef HAVE_DIM
#include <linux/dim.h>
#else
#include "bnxt_dim.h"
#endif
#include "bnxt_hsi.h"
#include "bnxt.h"

void bnxt_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct bnxt_cp_ring_info *cpr = container_of(dim,
						     struct bnxt_cp_ring_info,
						     dim);
	struct bnxt_napi *bnapi = container_of(cpr,
					       struct bnxt_napi,
					       cp_ring);
	struct dim_cq_moder cur_moder =
		net_dim_get_rx_moderation(dim->mode, dim->profile_ix);

	cpr->rx_ring_coal.coal_ticks = cur_moder.usec;
	cpr->rx_ring_coal.coal_bufs = cur_moder.pkts;

	bnxt_hwrm_set_ring_coal(bnapi->bp, bnapi);
	dim->state = DIM_START_MEASURE;
}

#ifndef HAVE_DIM
void net_dim(struct dim *dim, struct dim_sample end_sample)
{
	struct dim_stats curr_stats;
	u16 nevents;

	switch (dim->state) {
	case DIM_MEASURE_IN_PROGRESS:
		nevents = BIT_GAP(BITS_PER_TYPE(u16),
				  end_sample.event_ctr,
				  dim->start_sample.event_ctr);
		if (nevents < DIM_NEVENTS)
			break;
		dim_calc_stats(&dim->start_sample, &end_sample, &curr_stats);
		if (net_dim_decision(&curr_stats, dim)) {
			dim->state = DIM_APPLY_NEW_PROFILE;
			schedule_work(&dim->work);
			break;
		}
		fallthrough;
	case DIM_START_MEASURE:
		dim->state = DIM_MEASURE_IN_PROGRESS;
		break;
	case DIM_APPLY_NEW_PROFILE:
		break;
	}
}
#endif
