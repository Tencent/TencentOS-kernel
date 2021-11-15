// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_dcb_lib.h"
#include "ice_ptp.h"

DEFINE_IDA(ice_peer_index_ida);



static struct mfd_cell ice_mfd_cells[] = ASSIGN_PEER_INFO;

/**
 * ice_is_vsi_state_nominal
 * @vsi: pointer to the VSI struct
 *
 * returns true if VSI state is nominal, false otherwise
 */
static bool ice_is_vsi_state_nominal(struct ice_vsi *vsi)
{
	if (!vsi)
		return false;

	if (test_bit(ICE_VSI_DOWN, vsi->state) ||
	    test_bit(ICE_VSI_NEEDS_RESTART, vsi->state))
		return false;

	return true;
}


/**
 * ice_peer_state_change - manage state machine for peer
 * @peer_obj: pointer to peer's configuration
 * @new_state: the state requested to transition into
 * @locked: boolean to determine if call made with mutex held
 *
 * This function handles all state transitions for peer objects.
 *
 * The state machine is as follows:
 *
 *     +<-----------------------+<-----------------------------+
 *				|<-------+<----------+	       +
 *				\/	 +	     +	       +
 *    INIT  --------------> PROBED --> OPENING	  CLOSED --> REMOVED
 *					 +           +
 *				       OPENED --> CLOSING
 *					 +	     +
 *				       PREP_RST	     +
 *					 +	     +
 *				      PREPPED	     +
 *					 +---------->+
 *
 * NOTE: there is an error condition that can take a peer from OPENING
 * to REMOVED.
 */
static void
ice_peer_state_change(struct ice_peer_obj_int *peer_obj, long new_state,
		      bool locked)
{
	struct device *dev;

	dev = bus_find_device_by_name(&platform_bus_type, NULL,
				      peer_obj->plat_name);

	if (!locked)
		mutex_lock(&peer_obj->peer_obj_state_mutex);

	switch (new_state) {
	case ICE_PEER_OBJ_STATE_INIT:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_REMOVED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_INIT, peer_obj->state);
			dev_dbg(dev, "state transition from _REMOVED to _INIT\n");
		} else {
			set_bit(ICE_PEER_OBJ_STATE_INIT, peer_obj->state);
			if (dev)
				dev_dbg(dev, "state set to _INIT\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_PROBED:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_INIT,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_PROBED, peer_obj->state);
			dev_dbg(dev, "state transition from _INIT to _PROBED\n");
		} else if (test_and_clear_bit(ICE_PEER_OBJ_STATE_REMOVED,
					      peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_PROBED, peer_obj->state);
			dev_dbg(dev, "state transition from _REMOVED to _PROBED\n");
		} else if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENING,
					      peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_PROBED, peer_obj->state);
			dev_dbg(dev, "state transition from _OPENING to _PROBED\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_OPENING:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_PROBED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_OPENING, peer_obj->state);
			dev_dbg(dev, "state transition from _PROBED to _OPENING\n");
		} else if (test_and_clear_bit(ICE_PEER_OBJ_STATE_CLOSED,
					      peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_OPENING, peer_obj->state);
			dev_dbg(dev, "state transition from _CLOSED to _OPENING\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_OPENED:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENING,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_OPENED, peer_obj->state);
			dev_dbg(dev, "state transition from _OPENING to _OPENED\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_PREP_RST:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_PREP_RST, peer_obj->state);
			dev_dbg(dev, "state transition from _OPENED to _PREP_RST\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_PREPPED:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_PREP_RST,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_PREPPED, peer_obj->state);
			dev_dbg(dev, "state transition _PREP_RST to _PREPPED\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_CLOSING:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_CLOSING, peer_obj->state);
			dev_dbg(dev, "state transition from _OPENED to _CLOSING\n");
		}
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_PREPPED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_CLOSING, peer_obj->state);
			dev_dbg(dev, "state transition _PREPPED to _CLOSING\n");
		}
		/* NOTE - up to peer to handle this situation correctly */
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_PREP_RST,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_CLOSING, peer_obj->state);
			dev_warn(dev,
				 "WARN: Peer state _PREP_RST to _CLOSING\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_CLOSED:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_CLOSING,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_CLOSED, peer_obj->state);
			dev_dbg(dev, "state transition from _CLOSING to _CLOSED\n");
		}
		break;
	case ICE_PEER_OBJ_STATE_REMOVED:
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENED,
				       peer_obj->state) ||
		    test_and_clear_bit(ICE_PEER_OBJ_STATE_CLOSED,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_REMOVED, peer_obj->state);
			dev_dbg(dev, "state from _OPENED/_CLOSED to _REMOVED\n");
			/* Clear registration for events when peer removed */
			bitmap_zero(peer_obj->events, ICE_PEER_OBJ_STATE_NBITS);
		}
		if (test_and_clear_bit(ICE_PEER_OBJ_STATE_OPENING,
				       peer_obj->state)) {
			set_bit(ICE_PEER_OBJ_STATE_REMOVED, peer_obj->state);
			dev_warn(dev, "Peer failed to open, set to _REMOVED");
		}
		break;
	default:
		break;
	}

	if (!locked)
		mutex_unlock(&peer_obj->peer_obj_state_mutex);

	put_device(dev);
}

/**
 * ice_peer_close - close a peer object
 * @peer_obj_int: peer object to close
 * @data: pointer to opaque data
 *
 * This function will also set the state bit for the peer to CLOSED. This
 * function is meant to be called from a ice_for_each_peer().
 */
int ice_peer_close(struct ice_peer_obj_int *peer_obj_int, void *data)
{
	enum ice_close_reason reason = *(enum ice_close_reason *)(data);
	struct ice_peer_obj *peer_obj;
	struct ice_pf *pf;
	int i;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	/* return 0 so ice_for_each_peer will continue closing other peers */
	if (!ice_validate_peer_obj(peer_obj))
		return 0;
	pf = pci_get_drvdata(peer_obj->pdev);

	if (test_bit(ICE_DOWN, pf->state) ||
	    test_bit(ICE_SUSPENDED, pf->state) ||
	    test_bit(ICE_NEEDS_RESTART, pf->state))
		return 0;

	mutex_lock(&peer_obj_int->peer_obj_state_mutex);

	/* no peer driver, already closed, closing or opening nothing to do */
	if (test_bit(ICE_PEER_OBJ_STATE_CLOSED, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_CLOSING, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_OPENING, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_PROBED, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_REMOVED, peer_obj_int->state))
		goto peer_close_out;

	/* Set the peer state to CLOSING */
	ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSING, true);

	for (i = 0; i < ICE_EVENT_NBITS; i++)
		bitmap_zero(peer_obj_int->current_events[i].type,
			    ICE_EVENT_NBITS);

	if (peer_obj->peer_ops && peer_obj->peer_ops->close)
		peer_obj->peer_ops->close(peer_obj, reason);

	/* Set the peer state to CLOSED */
	ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSED, true);

peer_close_out:
	mutex_unlock(&peer_obj_int->peer_obj_state_mutex);

	return 0;
}

/**
 * ice_close_peer_for_reset - queue work to close peer for reset
 * @peer_obj_int: pointer peer object internal struct
 * @data: pointer to opaque data used for reset type
 */
int ice_close_peer_for_reset(struct ice_peer_obj_int *peer_obj_int, void *data)
{
	struct ice_peer_obj *peer_obj;
	enum ice_reset_req reset;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	if (!ice_validate_peer_obj(peer_obj) ||
	    (!test_bit(ICE_PEER_OBJ_STATE_OPENED, peer_obj_int->state) &&
	     !test_bit(ICE_PEER_OBJ_STATE_PREPPED, peer_obj_int->state)))
		return 0;

	reset = *(enum ice_reset_req *)data;

	switch (reset) {
	case ICE_RESET_EMPR:
		peer_obj_int->rst_type = ICE_REASON_EMPR_REQ;
		break;
	case ICE_RESET_GLOBR:
		peer_obj_int->rst_type = ICE_REASON_GLOBR_REQ;
		break;
	case ICE_RESET_CORER:
		peer_obj_int->rst_type = ICE_REASON_CORER_REQ;
		break;
	case ICE_RESET_PFR:
		peer_obj_int->rst_type = ICE_REASON_PFR_REQ;
		break;
	default:
		/* reset type is invalid */
		return 1;
	}
	queue_work(peer_obj_int->ice_peer_wq, &peer_obj_int->peer_close_task);
	return 0;
}

/**
 * ice_check_peer_drv_for_events - check peer_drv for events to report
 * @peer_obj: peer object to report to
 */
static void ice_check_peer_drv_for_events(struct ice_peer_obj *peer_obj)
{
	const struct ice_peer_ops *p_ops = peer_obj->peer_ops;
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_peer_drv_int *peer_drv_int;
	int i;

	peer_obj_int = peer_to_ice_obj_int(peer_obj);
	if (!peer_obj_int)
		return;
	peer_drv_int = peer_obj_int->peer_drv_int;

	for_each_set_bit(i, peer_obj_int->events, ICE_EVENT_NBITS) {
		struct ice_event *curr = &peer_drv_int->current_events[i];

		if (!bitmap_empty(curr->type, ICE_EVENT_NBITS) &&
		    p_ops->event_handler)
			p_ops->event_handler(peer_obj, curr);
	}
}

/**
 * ice_check_peer_for_events - check peer_objs for events new peer reg'd for
 * @src_peer_int: peer to check for events
 * @data: ptr to opaque data, to be used for the peer struct that opened
 *
 * This function is to be called when a peer object is opened.
 *
 * Since a new peer opening would have missed any events that would
 * have happened before its opening, we need to walk the peers and see
 * if any of them have events that the new peer cares about
 *
 * This function is meant to be called by a ice_for_each_peer.
 */
static int
ice_check_peer_for_events(struct ice_peer_obj_int *src_peer_int, void *data)
{
	struct ice_peer_obj *new_peer = (struct ice_peer_obj *)data;
	const struct ice_peer_ops *p_ops = new_peer->peer_ops;
	struct ice_peer_obj_int *new_peer_int;
	struct ice_peer_obj *src_peer;
	unsigned long i;

	src_peer = ice_get_peer_obj(src_peer_int);
	if (!ice_validate_peer_obj(new_peer) ||
	    !ice_validate_peer_obj(src_peer))
		return 0;

	new_peer_int = peer_to_ice_obj_int(new_peer);

	for_each_set_bit(i, new_peer_int->events, ICE_EVENT_NBITS) {
		struct ice_event *curr = &src_peer_int->current_events[i];

		if (!bitmap_empty(curr->type, ICE_EVENT_NBITS) &&
		    new_peer->peer_obj_id != src_peer->peer_obj_id &&
		    p_ops->event_handler)
			p_ops->event_handler(new_peer, curr);
	}

	return 0;
}

/**
 * ice_for_each_peer - iterate across and call function for each peer obj
 * @pf: pointer to private board struct
 * @data: data to pass to function on each call
 * @fn: pointer to function to call for each peer
 */
int
ice_for_each_peer(struct ice_pf *pf, void *data,
		  int (*fn)(struct ice_peer_obj_int *, void *))
{
	unsigned int i;

	if (!pf->peers)
		return 0;

	for (i = 0; i < ARRAY_SIZE(ice_mfd_cells); i++) {
		struct ice_peer_obj_int *peer_obj_int;

		peer_obj_int = pf->peers[i];
		if (peer_obj_int) {
			int ret = fn(peer_obj_int, data);

			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * ice_finish_init_peer_obj - complete peer object initialization
 * @peer_obj_int: ptr to peer object internal struct
 * @data: ptr to opaque data
 *
 * This function completes remaining initialization of peer objects
 */
int
ice_finish_init_peer_obj(struct ice_peer_obj_int *peer_obj_int,
			 void __always_unused *data)
{
	struct ice_peer_obj *peer_obj;
	struct ice_peer_drv *peer_drv;
	struct device *dev;
	struct ice_pf *pf;
	int ret = 0;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	/* peer_obj will not always be populated at the time of this check */
	if (!ice_validate_peer_obj(peer_obj))
		return ret;

	peer_drv = peer_obj->peer_drv;
	pf = pci_get_drvdata(peer_obj->pdev);
	dev = ice_pf_to_dev(pf);
	/* There will be several assessments of the peer_obj's state in this
	 * chunk of logic.  We need to hold the peer_obj_int's state mutex
	 * for the entire part so that the flow progresses without another
	 * context changing things mid-flow
	 */
	mutex_lock(&peer_obj_int->peer_obj_state_mutex);

	if (!peer_obj->peer_ops) {
		dev_err(dev, "peer_ops not defined in peer obj\n");
		goto init_unlock;
	}

	if (!peer_obj->peer_ops->open) {
		dev_err(dev, "peer_ops:open not defined in peer obj\n");
		goto init_unlock;
	}

	if (!peer_obj->peer_ops->close) {
		dev_err(dev, "peer_ops:close not defined in peer obj\n");
		goto init_unlock;
	}

	/* Peer driver expected to set driver_id during registration */
	if (!peer_drv->driver_id) {
		dev_err(dev, "Peer driver did not set driver_id\n");
		goto init_unlock;
	}

	if ((test_bit(ICE_PEER_OBJ_STATE_CLOSED, peer_obj_int->state) ||
	     test_bit(ICE_PEER_OBJ_STATE_PROBED, peer_obj_int->state)) &&
	    ice_pf_state_is_nominal(pf)) {
		/* If the RTNL is locked, we defer opening the peer
		 * until the next time this function is called by the
		 * service task.
		 */
		if (rtnl_is_locked())
			goto init_unlock;
		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_OPENING,
				      true);
		ret = peer_obj->peer_ops->open(peer_obj);
		if (ret == -EAGAIN) {
			dev_err(dev, "Peer %d failed to open\n",
				peer_obj->peer_obj_id);
			ice_peer_state_change(peer_obj_int,
					      ICE_PEER_OBJ_STATE_PROBED, true);
			goto init_unlock;
		} else if (ret) {
			ice_peer_state_change(peer_obj_int,
					      ICE_PEER_OBJ_STATE_REMOVED, true);
			peer_obj->peer_ops = NULL;
			goto init_unlock;
		}

		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_OPENED,
				      true);
		ret = ice_for_each_peer(pf, peer_obj,
					ice_check_peer_for_events);
		ice_check_peer_drv_for_events(peer_obj);
	}

	if (test_bit(ICE_PEER_OBJ_STATE_PREPPED, peer_obj_int->state)) {
		enum ice_close_reason reason = ICE_REASON_CORER_REQ;
		int i;

		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSING,
				      true);
		for (i = 0; i < ICE_EVENT_NBITS; i++)
			bitmap_zero(peer_obj_int->current_events[i].type,
				    ICE_EVENT_NBITS);

		peer_obj->peer_ops->close(peer_obj, reason);

		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSED,
				      true);
	}

init_unlock:
	mutex_unlock(&peer_obj_int->peer_obj_state_mutex);

	return ret;
}

/**
 * ice_unreg_peer_obj - unregister specified peer object
 * @peer_obj_int: ptr to peer object internal
 * @data: ptr to opaque data
 *
 * This function invokes object unregistration, removes ID associated with
 * the specified object.
 */
int ice_unreg_peer_obj(struct ice_peer_obj_int *peer_obj_int, void __always_unused *data)
{
	struct ice_peer_drv_int *peer_drv_int;
	struct ice_peer_obj *peer_obj;
	struct pci_dev *pdev;
	struct device *dev;
	struct ice_pf *pf;

	if (!peer_obj_int)
		return 0;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	pdev = peer_obj->pdev;
	if (!pdev)
		return 0;

	pf = pci_get_drvdata(pdev);
	if (!pf)
		return 0;
	dev = ice_pf_to_dev(pf);

	mfd_remove_devices(&pdev->dev);

	peer_drv_int = peer_obj_int->peer_drv_int;

	if (peer_obj_int->ice_peer_wq) {
		if (peer_obj_int->peer_prep_task.func)
			cancel_work_sync(&peer_obj_int->peer_prep_task);

		if (peer_obj_int->peer_close_task.func)
			cancel_work_sync(&peer_obj_int->peer_close_task);
		destroy_workqueue(peer_obj_int->ice_peer_wq);
	}

	devm_kfree(dev, peer_drv_int);

	devm_kfree(dev, peer_obj_int);

	return 0;
}

/**
 * ice_unroll_peer - destroy peers and peer_wq in case of error
 * @peer_obj_int: ptr to peer object internal struct
 * @data: ptr to opaque data
 *
 * This function releases resources in the event of a failure in creating
 * peer objects or their individual work_queues. Meant to be called from
 * a ice_for_each_peer invocation
 */
int ice_unroll_peer(struct ice_peer_obj_int *peer_obj_int, void __always_unused *data)
{
	struct ice_peer_obj *peer_obj;
	struct ice_pf *pf;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	if (!peer_obj || !peer_obj->pdev)
		return 0;

	pf = pci_get_drvdata(peer_obj->pdev);
	if (!pf)
		return 0;

	if (peer_obj_int->ice_peer_wq)
		destroy_workqueue(peer_obj_int->ice_peer_wq);

	if (peer_obj_int->peer_drv_int)
		devm_kfree(ice_pf_to_dev(pf), peer_obj_int->peer_drv_int);

	devm_kfree(ice_pf_to_dev(pf), peer_obj_int);

	return 0;
}

#ifdef CONFIG_PM
/**
 * ice_peer_refresh_msix - load new values into ice_peer_obj structs
 * @pf: pointer to private board struct
 */
void ice_peer_refresh_msix(struct ice_pf *pf)
{
	struct ice_peer_obj *peer;
	unsigned int i;

	if (!pf->peers)
		return;

	for (i = 0; i < ARRAY_SIZE(ice_mfd_cells); i++) {
		if (!pf->peers[i])
			continue;

		peer = ice_get_peer_obj(pf->peers[i]);
		if (!peer)
			continue;

		switch (peer->peer_obj_id) {
		case ICE_PEER_RDMA_ID:
			peer->msix_count = pf->num_rdma_msix;
			peer->msix_entries = &pf->msix_entries[pf->rdma_base_vector];
			break;
		default:
			break;
		}
	}
}

#endif /* CONFIG_PM */
/**
 * ice_find_vsi - Find the VSI from VSI ID
 * @pf: The PF pointer to search in
 * @vsi_num: The VSI ID to search for
 */
static struct ice_vsi *ice_find_vsi(struct ice_pf *pf, u16 vsi_num)
{
	int i;

	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->vsi_num == vsi_num)
			return  pf->vsi[i];
	return NULL;
}

/**
 * ice_peer_alloc_rdma_qsets - Allocate Leaf Nodes for RDMA Qset
 * @peer_obj: peer that is requesting the Leaf Nodes
 * @res: Resources to be allocated
 * @partial_acceptable: If partial allocation is acceptable to the peer
 *
 * This function allocates Leaf Nodes for given RDMA Qset resources
 * for the peer object.
 */
static int
ice_peer_alloc_rdma_qsets(struct ice_peer_obj *peer_obj, struct ice_res *res,
			  int __always_unused partial_acceptable)
{
	u16 max_rdmaqs[ICE_MAX_TRAFFIC_CLASS];
	enum ice_status status;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	int i, ret = 0;
	u32 *qset_teid;
	u16 *qs_handle;

	if (!ice_validate_peer_obj(peer_obj) || !res)
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);
	dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_FLAG_IWARP_ENA, pf->flags))
		return -EINVAL;

	if (res->cnt_req > ICE_MAX_TXQ_PER_TXQG)
		return -EINVAL;

	qset_teid = kcalloc(res->cnt_req, sizeof(*qset_teid), GFP_KERNEL);
	if (!qset_teid)
		return -ENOMEM;

	qs_handle = kcalloc(res->cnt_req, sizeof(*qs_handle), GFP_KERNEL);
	if (!qs_handle) {
		kfree(qset_teid);
		return -ENOMEM;
	}

	ice_for_each_traffic_class(i)
		max_rdmaqs[i] = 0;

	for (i = 0; i < res->cnt_req; i++) {
		struct ice_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		if (qset->vsi_id != peer_obj->pf_vsi_num) {
			dev_err(dev, "RDMA QSet invalid VSI requested\n");
			ret = -EINVAL;
			goto out;
		}
		max_rdmaqs[qset->tc]++;
		qs_handle[i] = qset->qs_handle;
	}

	vsi = ice_find_vsi(pf, peer_obj->pf_vsi_num);
	if (!vsi) {
		dev_err(dev, "RDMA QSet invalid VSI\n");
		ret = -EINVAL;
		goto out;
	}

	status = ice_cfg_vsi_rdma(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
				  max_rdmaqs);
	if (status) {
		dev_err(dev, "Failed VSI RDMA qset config\n");
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < res->cnt_req; i++) {
		struct ice_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		status = ice_ena_vsi_rdma_qset(vsi->port_info, vsi->idx,
					       qset->tc, &qs_handle[i], 1,
					       &qset_teid[i]);
		if (status) {
			dev_err(dev, "Failed VSI RDMA qset enable\n");
			ret = -EINVAL;
			goto out;
		}
		vsi->qset_handle[qset->tc] = qset->qs_handle;
		qset->teid = qset_teid[i];
	}

out:
	kfree(qset_teid);
	kfree(qs_handle);
	return ret;
}

/**
 * ice_peer_free_rdma_qsets - Free leaf nodes for RDMA Qset
 * @peer_obj: peer that requested qsets to be freed
 * @res: Resource to be freed
 */
static int
ice_peer_free_rdma_qsets(struct ice_peer_obj *peer_obj, struct ice_res *res)
{
	enum ice_status status;
	int count, i, ret = 0;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	u16 vsi_id;
	u32 *teid;
	u16 *q_id;

	if (!ice_validate_peer_obj(peer_obj) || !res)
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);
	dev = ice_pf_to_dev(pf);

	count = res->res_allocated;
	if (count > ICE_MAX_TXQ_PER_TXQG)
		return -EINVAL;

	teid = kcalloc(count, sizeof(*teid), GFP_KERNEL);
	if (!teid)
		return -ENOMEM;

	q_id = kcalloc(count, sizeof(*q_id), GFP_KERNEL);
	if (!q_id) {
		kfree(teid);
		return -ENOMEM;
	}

	vsi_id = res->res[0].res.qsets.vsi_id;
	vsi = ice_find_vsi(pf, vsi_id);
	if (!vsi) {
		dev_err(dev, "RDMA Invalid VSI\n");
		ret = -EINVAL;
		goto rdma_free_out;
	}

	for (i = 0; i < count; i++) {
		struct ice_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		if (qset->vsi_id != vsi_id) {
			dev_err(dev, "RDMA Invalid VSI ID\n");
			ret = -EINVAL;
			goto rdma_free_out;
		}
		q_id[i] = qset->qs_handle;
		teid[i] = qset->teid;

		vsi->qset_handle[qset->tc] = 0;
	}

	status = ice_dis_vsi_rdma_qset(vsi->port_info, count, teid, q_id);
	if (status)
		ret = -EINVAL;

rdma_free_out:
	kfree(teid);
	kfree(q_id);

	return ret;
}

/**
 * ice_peer_alloc_res - Allocate requested resources for peer objects
 * @peer_obj: peer that is requesting resources
 * @res: Resources to be allocated
 * @partial_acceptable: If partial allocation is acceptable to the peer
 *
 * This function allocates requested resources for the peer object.
 */
static int
ice_peer_alloc_res(struct ice_peer_obj *peer_obj, struct ice_res *res,
		   int partial_acceptable)
{
	struct ice_pf *pf;
	int ret;

	if (!ice_validate_peer_obj(peer_obj) || !res)
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);
	if (!ice_pf_state_is_nominal(pf))
		return -EBUSY;

	switch (res->res_type) {
	case ICE_RDMA_QSETS_TXSCHED:
		ret = ice_peer_alloc_rdma_qsets(peer_obj, res,
						partial_acceptable);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ice_peer_free_res - Free given resources
 * @peer_obj: peer that is requesting freeing of resources
 * @res: Resources to be freed
 *
 * Free/Release resources allocated to given peer onjects.
 */
static int
ice_peer_free_res(struct ice_peer_obj *peer_obj, struct ice_res *res)
{
	int ret;

	if (!ice_validate_peer_obj(peer_obj) || !res)
		return -EINVAL;

	switch (res->res_type) {
	case ICE_RDMA_QSETS_TXSCHED:
		ret = ice_peer_free_rdma_qsets(peer_obj, res);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ice_peer_reg_for_notif - register a peer to receive specific notifications
 * @peer_obj: peer that is registering for event notifications
 * @events: mask of event types peer is registering for
 */
static void
ice_peer_reg_for_notif(struct ice_peer_obj *peer_obj, struct ice_event *events)
{
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_pf *pf;

	if (!ice_validate_peer_obj(peer_obj) || !events)
		return;

	peer_obj_int = peer_to_ice_obj_int(peer_obj);
	pf = pci_get_drvdata(peer_obj->pdev);

	bitmap_or(peer_obj_int->events, peer_obj_int->events, events->type,
		  ICE_EVENT_NBITS);

	/* Check to see if any events happened previous to peer registering */
	ice_for_each_peer(pf, peer_obj, ice_check_peer_for_events);
	ice_check_peer_drv_for_events(peer_obj);
}

/**
 * ice_peer_unreg_for_notif - unreg a peer from receiving certain notifications
 * @peer_obj: peer that is unregistering from event notifications
 * @events: mask of event types peer is unregistering for
 */
static void
ice_peer_unreg_for_notif(struct ice_peer_obj *peer_obj,
			 struct ice_event *events)
{
	struct ice_peer_obj_int *peer_obj_int;

	if (!ice_validate_peer_obj(peer_obj) || !events)
		return;

	peer_obj_int = peer_to_ice_obj_int(peer_obj);

	bitmap_andnot(peer_obj_int->events, peer_obj_int->events, events->type,
		      ICE_EVENT_NBITS);
}

/**
 * ice_peer_check_for_reg - check to see if any peers are reg'd for event
 * @peer_obj_int: ptr to peer object internal struct
 * @data: ptr to opaque data, to be used for ice_event to report
 *
 * This function is to be called by ice_for_each_peer to handle an
 * event reported by a peer or the ice driver.
 */
int ice_peer_check_for_reg(struct ice_peer_obj_int *peer_obj_int, void *data)
{
	struct ice_event *event = (struct ice_event *)data;
	DECLARE_BITMAP(comp_events, ICE_EVENT_NBITS);
	struct ice_peer_obj *peer_obj;
	bool check = true;

	peer_obj = ice_get_peer_obj(peer_obj_int);

	if (!ice_validate_peer_obj(peer_obj) || !data)
	/* If invalid obj, in this case return 0 instead of error
	 * because caller ignores this return value
	 */
		return 0;

	if (event->reporter)
		check = event->reporter->peer_obj_id != peer_obj->peer_obj_id;

	if (bitmap_and(comp_events, event->type, peer_obj_int->events,
		       ICE_EVENT_NBITS) &&
	    (test_bit(ICE_PEER_OBJ_STATE_OPENED, peer_obj_int->state) ||
	     test_bit(ICE_PEER_OBJ_STATE_PREP_RST, peer_obj_int->state) ||
	     test_bit(ICE_PEER_OBJ_STATE_PREPPED, peer_obj_int->state)) &&
	    check &&
	    peer_obj->peer_ops->event_handler)
		peer_obj->peer_ops->event_handler(peer_obj, event);

	return 0;
}

/**
 * ice_peer_report_state_change - accept report of a peer state change
 * @peer_obj: peer that is sending notification about state change
 * @event: ice_event holding info on what the state change is
 *
 * We also need to parse the list of peers to see if anyone is registered
 * for notifications about this state change event, and if so, notify them.
 */
static void
ice_peer_report_state_change(struct ice_peer_obj *peer_obj,
			     struct ice_event *event)
{
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_peer_drv_int *peer_drv_int;
	unsigned int e_type;
	int drv_event = 0;
	struct ice_pf *pf;

	if (!ice_validate_peer_obj(peer_obj) || !event)
		return;

	pf = pci_get_drvdata(peer_obj->pdev);
	peer_obj_int = peer_to_ice_obj_int(peer_obj);
	peer_drv_int = peer_obj_int->peer_drv_int;

	e_type = find_first_bit(event->type, ICE_EVENT_NBITS);
	if (!e_type)
		return;

	switch (e_type) {
	/* Check for peer_drv events */
	case ICE_EVENT_MBX_CHANGE:
		drv_event = 1;
		if (event->info.mbx_rdy)
			set_bit(ICE_PEER_DRV_STATE_MBX_RDY,
				peer_drv_int->state);
		else
			clear_bit(ICE_PEER_DRV_STATE_MBX_RDY,
				  peer_drv_int->state);
		break;

	/* Check for peer_obj events */
	case ICE_EVENT_API_CHANGE:
		if (event->info.api_rdy) {
			set_bit(ICE_PEER_OBJ_STATE_API_RDY,
				peer_obj_int->state);
		} else {
			clear_bit(ICE_PEER_OBJ_STATE_API_RDY,
				  peer_obj_int->state);
		}
		break;

	default:
		return;
	}

	/* store the event and state to notify any new peers opening */
	if (drv_event)
		memcpy(&peer_drv_int->current_events[e_type], event,
		       sizeof(*event));
	else
		memcpy(&peer_obj_int->current_events[e_type], event,
		       sizeof(*event));

	ice_for_each_peer(pf, event, ice_peer_check_for_reg);
}

/**
 * ice_peer_unregister - request to unregister peer
 * @peer_obj: peer object
 *
 * This function triggers close/remove on peer_obj allowing peer
 * to unregister.
 */
static int ice_peer_unregister(struct ice_peer_obj *peer_obj)
{
	enum ice_close_reason reason = ICE_REASON_PEER_DRV_UNREG;
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_pf *pf;
	int ret;

	if (!ice_validate_peer_obj(peer_obj))
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);
	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	peer_obj_int = peer_to_ice_obj_int(peer_obj);

	ret = ice_peer_close(peer_obj_int, &reason);
	if (ret)
		return ret;

	switch (peer_obj->peer_obj_id) {
	case ICE_PEER_RDMA_ID:
		pf->rdma_peer = NULL;
		break;
	default:
		break;
	}

	peer_obj->peer_ops = NULL;

	ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_REMOVED, false);
	return 0;
}

/**
 * ice_peer_register - Called by peer to open communication with LAN
 * @peer_obj: ptr to peer object
 *
 * registering peer is expected to populate the ice_peerdrv->name field
 * before calling this function.
 */
static int ice_peer_register(struct ice_peer_obj *peer_obj)
{
	struct ice_peer_drv_int *peer_drv_int;
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_peer_drv *peer_drv;


	if (!peer_obj) {
		pr_err("Failed to reg peer_obj: peer_obj ptr NULL\n");
		return -EINVAL;
	}

	if (!peer_obj->pdev) {
		pr_err("Failed to reg peer_obj: peer_obj pdev NULL\n");
		return -EINVAL;
	}

	if (!peer_obj->peer_ops || !peer_obj->ops) {
		pr_err("Failed to reg peer_obj: peer_obj peer_ops/ops NULL\n");
		return -EINVAL;
	}

	peer_drv = peer_obj->peer_drv;
	if (!peer_drv) {
		pr_err("Failed to reg peer_obj: peer drv NULL\n");
		return -EINVAL;
	}


	if (peer_drv->ver.major != ICE_PEER_MAJOR_VER ||
	    peer_drv->ver.minor != ICE_PEER_MINOR_VER) {
		pr_err("failed to register due to version mismatch:\n");
		pr_err("expected major ver %d, caller specified major ver %d\n",
		       ICE_PEER_MAJOR_VER, peer_drv->ver.major);
		pr_err("expected minor ver %d, caller specified minor ver %d\n",
		       ICE_PEER_MINOR_VER, peer_drv->ver.minor);
		return -EINVAL;
	}

	peer_obj_int = peer_to_ice_obj_int(peer_obj);
	peer_drv_int = peer_obj_int->peer_drv_int;
	if (!peer_drv_int) {
		pr_err("Failed to match peer_drv_int to peer_obj\n");
		return -EINVAL;
	}

	peer_drv_int->peer_drv = peer_drv;

	ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_PROBED, false);

	return 0;
}


/**
 * ice_peer_request_reset - accept request from peer to perform a reset
 * @peer_obj: peer object that is requesting a reset
 * @reset_type: type of reset the peer is requesting
 */
static int
ice_peer_request_reset(struct ice_peer_obj *peer_obj, enum ice_peer_reset_type reset_type)
{
	enum ice_reset_req reset;
	struct ice_pf *pf;

	if (!ice_validate_peer_obj(peer_obj))
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);

	switch (reset_type) {
	case ICE_PEER_PFR:
		reset = ICE_RESET_PFR;
		break;
	case ICE_PEER_CORER:
		reset = ICE_RESET_CORER;
		break;
	case ICE_PEER_GLOBR:
		reset = ICE_RESET_GLOBR;
		break;
	default:
		dev_err(ice_pf_to_dev(pf), "incorrect reset request from peer\n");
		return -EINVAL;
	}

	return ice_schedule_reset(pf, reset);
}

/**
 * ice_peer_is_vsi_ready - query if VSI in nominal state
 * @peer_obj: pointer to ice_peer_obj struct
 */
static int ice_peer_is_vsi_ready(struct ice_peer_obj *peer_obj)
{
	struct ice_netdev_priv *np;
	struct ice_vsi *vsi;

	/* If the peer_obj or associated values are not valid, then return
	 * 0 as there is no ready port associated with the values passed in
	 * as parameters.
	 */

	if (!peer_obj || !peer_obj->pdev || !pci_get_drvdata(peer_obj->pdev) ||
	    !peer_to_ice_obj_int(peer_obj))
		return 0;

	if (!peer_obj->netdev)
		return 0;

	np = netdev_priv(peer_obj->netdev);
	vsi = np->vsi;

	return ice_is_vsi_state_nominal(vsi);
}

/**
 * ice_peer_update_vsi_filter - update main VSI filters for RDMA
 * @peer_obj: pointer to RDMA peer object
 * @filter: selection of filters to enable or disable
 * @enable: bool whether to enable or disable filters
 */
static int
ice_peer_update_vsi_filter(struct ice_peer_obj *peer_obj,
			   enum ice_rdma_filter __maybe_unused filter,
			   bool enable)
{
	struct ice_vsi *vsi;
	struct ice_pf *pf;
	int ret;

	if (!ice_validate_peer_obj(peer_obj))
		return -EINVAL;

	pf = pci_get_drvdata(peer_obj->pdev);

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return -EINVAL;

	ret = ice_cfg_iwarp_fltr(&pf->hw, vsi->idx, enable);

	if (ret) {
		dev_err(ice_pf_to_dev(pf), "Failed to  %sable iWARP filtering\n",
			enable ? "en" : "dis");
	} else {
		if (enable)
			vsi->info.q_opt_flags |= ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
		else
			vsi->info.q_opt_flags &= ~ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
	}

	return ret;
}

/**
 * ice_peer_vc_send - send a virt channel message from a peer
 * @peer_obj: pointer to a peer object
 * @vf_id: the absolute VF ID of recipient of message
 * @msg: pointer to message contents
 * @len: len of message
 */
static int
ice_peer_vc_send(struct ice_peer_obj *peer_obj, u32 vf_id, u8 *msg, u16 len)
{
	enum ice_status status;
	struct ice_pf *pf;

	if (!ice_validate_peer_obj(peer_obj))
		return -EINVAL;
	if (!msg || !len)
		return -ENOMEM;

	pf = pci_get_drvdata(peer_obj->pdev);
	if (len > ICE_AQ_MAX_BUF_LEN)
		return -EINVAL;

	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	switch (peer_obj->peer_drv->driver_id) {
	case ICE_PEER_RDMA_DRIVER:
		if (vf_id >= pf->num_alloc_vfs)
			return -ENODEV;

		/* VIRTCHNL_OP_RDMA is being used for RoCEv2 msg also */
		status = ice_aq_send_msg_to_vf(&pf->hw, vf_id, VIRTCHNL_OP_RDMA,
					       0, msg, len, NULL);
		break;
	default:
		dev_err(ice_pf_to_dev(pf),
			"Peer driver (%u) not supported!", (u32)peer_obj->peer_drv->driver_id);
		return -ENODEV;
	}

	if (status)
		dev_err(ice_pf_to_dev(pf), "Unable to send msg to VF, error %s\n",
			ice_stat_str(status));
	return ice_status_to_errno(status);
}

/**
 * ice_reserve_peer_qvector - Reserve vector resources for peer drivers
 * @pf: board private structure to initialize
 */
static int ice_reserve_peer_qvector(struct ice_pf *pf)
{
	if (test_bit(ICE_FLAG_IWARP_ENA, pf->flags)) {
		int index;

		index = ice_get_res(pf, pf->irq_tracker, pf->num_rdma_msix, ICE_RES_RDMA_VEC_ID);
		if (index < 0)
			return index;
		pf->num_avail_sw_msix -= pf->num_rdma_msix;
		pf->rdma_base_vector = (u16)index;
	}
	return 0;
}

/**
 * ice_peer_close_task - call peer's close asynchronously
 * @work: pointer to work_struct contained by the peer_obj_int struct
 *
 * This method (asynchronous) of calling a peer's close function is
 * meant to be used in the reset path.
 */
static void ice_peer_close_task(struct work_struct *work)
{
	struct ice_peer_obj_int *peer_obj_int;
	struct ice_peer_obj *peer_obj;

	peer_obj_int = container_of(work, struct ice_peer_obj_int, peer_close_task);

	peer_obj = ice_get_peer_obj(peer_obj_int);
	if (!peer_obj || !peer_obj->peer_ops)
		return;

	/* If this peer_obj is going to close, we do not want any state changes
	 * to happen until after we successfully finish or abort the close.
	 * Grab the peer_obj_state_mutex to protect this flow
	 */
	mutex_lock(&peer_obj_int->peer_obj_state_mutex);

	/* Only allow a close to go to the peer if they are in a state
	 * to accept it. The last state of PREP_RST is a special case
	 * that will not normally happen, but it is up to the peer
	 * to handle it correctly.
	 */
	if (test_bit(ICE_PEER_OBJ_STATE_OPENED, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_PREPPED, peer_obj_int->state) ||
	    test_bit(ICE_PEER_OBJ_STATE_PREP_RST, peer_obj_int->state)) {

		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSING, true);

		if (peer_obj->peer_ops->close)
			peer_obj->peer_ops->close(peer_obj, peer_obj_int->rst_type);

		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_CLOSED, true);
	}

	mutex_unlock(&peer_obj_int->peer_obj_state_mutex);
}

/**
 * ice_peer_update_vsi - update the pf_vsi info in peer_obj struct
 * @peer_obj_int: pointer to peer_obj internal struct
 * @data: opaque pointer - VSI to be updated
 */
int ice_peer_update_vsi(struct ice_peer_obj_int *peer_obj_int, void *data)
{
	struct ice_vsi *vsi = (struct ice_vsi *)data;
	struct ice_peer_obj *peer_obj;

	peer_obj = ice_get_peer_obj(peer_obj_int);
	if (!peer_obj)
		return 0;

	peer_obj->pf_vsi_num = vsi->vsi_num;
	return 0;
}

/* Initialize the ice_ops struct, which is used in 'ice_init_peer_devices' */
static const struct ice_ops ops = {
	.alloc_res			= ice_peer_alloc_res,
	.free_res			= ice_peer_free_res,
	.is_vsi_ready			= ice_peer_is_vsi_ready,
	.reg_for_notification		= ice_peer_reg_for_notif,
	.unreg_for_notification		= ice_peer_unreg_for_notif,
	.notify_state_change		= ice_peer_report_state_change,
	.request_reset			= ice_peer_request_reset,
	.peer_register			= ice_peer_register,
	.peer_unregister		= ice_peer_unregister,
	.update_vsi_filter		= ice_peer_update_vsi_filter,
	.vc_send			= ice_peer_vc_send,

};

/**
 * ice_init_peer_devices - initializes peer objects and aux devices
 * @pf: ptr to ice_pf
 *
 * This function initializes peer objects and auxiliary device, then
 * associates them with specified pci_dev as their parent.
 */
int ice_init_peer_devices(struct ice_pf *pf)
{
	struct ice_port_info *port_info = pf->hw.port_info;
	struct ice_vsi *vsi = pf->vsi[0];
	struct pci_dev *pdev = pf->pdev;
	struct device *dev = &pdev->dev;
	int status = 0;
	unsigned int i;

	/* Reserve vector resources */
	status = ice_reserve_peer_qvector(pf);
	if (status < 0) {
		dev_err(dev, "failed to reserve vectors for peer drivers\n");
		return status;
	}
	for (i = 0; i < ARRAY_SIZE(ice_mfd_cells); i++) {
		struct ice_peer_obj_platform_data *platform_data;
		struct ice_peer_obj_int *peer_obj_int;
		struct ice_peer_drv_int *peer_drv_int;
		struct msix_entry *entry = NULL;
		struct ice_qos_params *qos_info;
		struct ice_peer_obj *peer_obj;
		int j;

		peer_obj_int = devm_kzalloc(dev, sizeof(*peer_obj_int),
					    GFP_KERNEL);
		if (!peer_obj_int)
			return -ENOMEM;
		pf->peers[i] = peer_obj_int;

		peer_drv_int = devm_kzalloc(dev, sizeof(*peer_drv_int),
					    GFP_KERNEL);
		if (!peer_drv_int)
			return -ENOMEM;

		peer_obj_int->peer_drv_int = peer_drv_int;

		/* Initialize driver values */
		for (j = 0; j < ICE_EVENT_NBITS; j++)
			bitmap_zero(peer_drv_int->current_events[j].type,
				    ICE_EVENT_NBITS);

		mutex_init(&peer_obj_int->peer_obj_state_mutex);

		peer_obj = ice_get_peer_obj(peer_obj_int);
		peer_obj_int->plat_data.peer_obj = peer_obj;
		platform_data = &peer_obj_int->plat_data;
		peer_obj->peer_ops = NULL;
		peer_obj->hw_addr = (u8 __iomem *)pf->hw.hw_addr;
		peer_obj->ver.major = ICE_PEER_MAJOR_VER;
		peer_obj->ver.minor = ICE_PEER_MINOR_VER;
		peer_obj->ver.support = ICE_IDC_FEATURES;
		peer_obj->peer_obj_id = ice_mfd_cells[i].id;
		peer_obj->pf_vsi_num = vsi->vsi_num;
		peer_obj->netdev = vsi->netdev;
		peer_obj->initial_mtu = vsi->netdev->mtu;
		ether_addr_copy(peer_obj->lan_addr, port_info->mac.lan_addr);

		ice_mfd_cells[i].platform_data = platform_data;
		ice_mfd_cells[i].pdata_size = sizeof(*platform_data);

		peer_obj_int->ice_peer_wq =
			alloc_ordered_workqueue("ice_peer_wq_%d", WQ_UNBOUND,
						i);
		if (!peer_obj_int->ice_peer_wq)
			return -ENOMEM;
		INIT_WORK(&peer_obj_int->peer_close_task, ice_peer_close_task);

		peer_obj->pdev = pdev;
		peer_obj->ari_ena = pci_ari_enabled(pdev->bus);
		peer_obj->bus_num = PCI_BUS_NUM(pdev->devfn);
		if (!peer_obj->ari_ena) {
			peer_obj->dev_num = PCI_SLOT(pdev->devfn);
			peer_obj->fn_num = PCI_FUNC(pdev->devfn);
		} else {
			peer_obj->dev_num = 0;
			peer_obj->fn_num = pdev->devfn & 0xff;
		}

		qos_info = &peer_obj->initial_qos_info;

		/* setup qos_info fields with defaults */
		qos_info->num_apps = 0;
		qos_info->num_tc = 1;

		for (j = 0; j < ICE_IDC_MAX_USER_PRIORITY; j++)
			qos_info->up2tc[j] = 0;

		qos_info->tc_info[0].rel_bw = 100;
		for (j = 1; j < IEEE_8021QAZ_MAX_TCS; j++)
			qos_info->tc_info[j].rel_bw = 0;

		/* for DCB, override the qos_info defaults. */
		ice_setup_dcb_qos_info(pf, qos_info);
		/* Initialize ice_ops */
		peer_obj->ops = &ops;

		/* make sure peer specific resources such as msix_count and
		 * msix_entries are initialized
		 */
		switch (ice_mfd_cells[i].id) {
		case ICE_PEER_RDMA_ID:
			if (test_bit(ICE_FLAG_IWARP_ENA, pf->flags)) {
				peer_obj->msix_count = pf->num_rdma_msix;
				entry = &pf->msix_entries[pf->rdma_base_vector];
			}
			pf->rdma_peer = peer_obj;
			break;
		default:
			break;
		}

		peer_obj->msix_entries = entry;
		ice_peer_state_change(peer_obj_int, ICE_PEER_OBJ_STATE_INIT,
				      false);
	}

	status = ida_simple_get(&ice_peer_index_ida, 0, 0, GFP_KERNEL);
	if (status < 0) {
		dev_err(&pdev->dev, "failed to get unique index for device\n");
		return status;
	}

	pf->peer_idx = status;

	status = mfd_add_devices(dev, pf->peer_idx, ice_mfd_cells,
				 ARRAY_SIZE(ice_mfd_cells), NULL, 0, NULL);
	if (status) {
		dev_err(dev, "Failure adding MFD devs for peers: %d\n", status);
		return status;
	}

	for (i = 0; i < ARRAY_SIZE(ice_mfd_cells); i++) {
		snprintf(pf->peers[i]->plat_name, ICE_MAX_PEER_NAME, "%s.%d",
			 ice_mfd_cells[i].name,
			 pf->peer_idx + ice_mfd_cells[i].id);
		dev = bus_find_device_by_name(&platform_bus_type, NULL,
					      pf->peers[i]->plat_name);
		if (dev) {
			dev_dbg(dev, "Peer Created: %s %d\n",
				pf->peers[i]->plat_name, pf->peer_idx);
			put_device(dev);
		}
	}

	return status;
}
