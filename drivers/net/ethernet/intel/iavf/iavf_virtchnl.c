// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2013, Intel Corporation. */

#include "iavf.h"
#include "iavf_prototype.h"

/* busy wait delay in msec */
#define IAVF_BUSY_WAIT_DELAY 10
#define IAVF_BUSY_WAIT_COUNT 50

/**
 * iavf_send_pf_msg
 * @adapter: adapter structure
 * @op: virtual channel opcode
 * @msg: pointer to message buffer
 * @len: message length
 *
 * Send message to PF and print status if failure.
 **/
static int iavf_send_pf_msg(struct iavf_adapter *adapter,
			    enum virtchnl_ops op, u8 *msg, u16 len)
{
	struct iavf_hw *hw = &adapter->hw;
	enum iavf_status err;

	if (adapter->flags & IAVF_FLAG_PF_COMMS_FAILED)
		return 0; /* nothing to see here, move along */

	err = iavf_aq_send_msg_to_pf(hw, op, VIRTCHNL_STATUS_SUCCESS, msg, len,
				     NULL);
	if (err)
		dev_dbg(&adapter->pdev->dev, "Unable to send opcode %d to PF, err %s, aq_err %s\n",
			op, iavf_stat_str(hw, err),
			iavf_aq_str(hw, hw->aq.asq_last_status));
	return err;
}

/**
 * iavf_send_api_ver
 * @adapter: adapter structure
 *
 * Send API version admin queue message to the PF. The reply is not checked
 * in this function. Returns 0 if the message was successfully
 * sent, or one of the IAVF_ADMIN_QUEUE_ERROR_ statuses if not.
 **/
int iavf_send_api_ver(struct iavf_adapter *adapter)
{
	struct virtchnl_version_info vvi;

	vvi.major = VIRTCHNL_VERSION_MAJOR;
	vvi.minor = VIRTCHNL_VERSION_MINOR;

	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_VERSION, (u8 *)&vvi,
				sizeof(vvi));
}

/**
 * iavf_verify_api_ver
 * @adapter: adapter structure
 *
 * Compare API versions with the PF. Must be called after admin queue is
 * initialized. Returns 0 if API versions match, -EIO if they do not,
 * IAVF_ERR_ADMIN_QUEUE_NO_WORK if the admin queue is empty, and any errors
 * from the firmware are propagated.
 **/
int iavf_verify_api_ver(struct iavf_adapter *adapter)
{
	struct virtchnl_version_info *pf_vvi;
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;

	event.buf_len = IAVF_MAX_AQ_BUF_SIZE;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		err = iavf_clean_arq_element(hw, &event, NULL);
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_VERSION)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	if (err)
		goto out_alloc;

	pf_vvi = (struct virtchnl_version_info *)event.msg_buf;
	adapter->pf_version = *pf_vvi;

	if ((pf_vvi->major > VIRTCHNL_VERSION_MAJOR) ||
	    ((pf_vvi->major == VIRTCHNL_VERSION_MAJOR) &&
	     (pf_vvi->minor > VIRTCHNL_VERSION_MINOR)))
		err = -EIO;

out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

/**
 * iavf_send_vf_config_msg
 * @adapter: adapter structure
 *
 * Send VF configuration request admin queue message to the PF. The reply
 * is not checked in this function. Returns 0 if the message was
 * successfully sent, or one of the IAVF_ADMIN_QUEUE_ERROR_ statuses if not.
 **/
int iavf_send_vf_config_msg(struct iavf_adapter *adapter)
{
	u32 caps;

	caps = VIRTCHNL_VF_OFFLOAD_L2 |
	       VIRTCHNL_VF_OFFLOAD_RSS_PF |
	       VIRTCHNL_VF_OFFLOAD_RSS_AQ |
	       VIRTCHNL_VF_OFFLOAD_RSS_REG |
	       VIRTCHNL_VF_OFFLOAD_VLAN |
	       VIRTCHNL_VF_OFFLOAD_WB_ON_ITR |
	       VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2 |
	       VIRTCHNL_VF_OFFLOAD_ENCAP |
	       VIRTCHNL_VF_OFFLOAD_VLAN_V2 |
	       VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC |
	       VIRTCHNL_VF_OFFLOAD_REQ_QUEUES |
	       VIRTCHNL_VF_CAP_PTP |
#ifdef __TC_MQPRIO_MODE_MAX
	       VIRTCHNL_VF_OFFLOAD_ADQ |
	       VIRTCHNL_VF_OFFLOAD_ADQ_V2 |
#endif /* __TC_MQPRIO_MODE_MAX */
	       VIRTCHNL_VF_OFFLOAD_USO |
#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
	       VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM |
	       VIRTCHNL_VF_CAP_ADV_LINK_SPEED;
#else
	       VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM;
#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */

	adapter->current_op = VIRTCHNL_OP_GET_VF_RESOURCES;
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_CONFIG;
	if (PF_IS_V11(adapter))
		return iavf_send_pf_msg(adapter,
					  VIRTCHNL_OP_GET_VF_RESOURCES,
					  (u8 *)&caps, sizeof(caps));
	else
		return iavf_send_pf_msg(adapter,
					  VIRTCHNL_OP_GET_VF_RESOURCES,
					  NULL, 0);
}

int iavf_send_vf_offload_vlan_v2_msg(struct iavf_adapter *adapter)
{
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_OFFLOAD_VLAN_V2_CAPS;

	if (!VLAN_V2_ALLOWED(adapter))
		return -EOPNOTSUPP;

	adapter->current_op = VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS;

	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS,
				NULL, 0);
}

int iavf_send_vf_supported_rxdids_msg(struct iavf_adapter *adapter)
{
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_SUPPORTED_RXDIDS;

	if (!RXDID_ALLOWED(adapter))
		return -EOPNOTSUPP;

	adapter->current_op = VIRTCHNL_OP_GET_SUPPORTED_RXDIDS;

	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_SUPPORTED_RXDIDS,
				NULL, 0);
}

/**
 * iavf_send_vf_ptp_caps_msg - Send request for PTP capabilities
 * @adapter: private adapter structure
 *
 * Send the VIRTCHNL_OP_1588_PTP_GET_CAPS command to the PF to request the PTP
 * capabilities available to this device. This includes the following
 * potential access:
 *
 * * READ_PHC - access to read the PTP hardware clock time
 * * WRITE_PHC - access to control the PHC time via adjustments
 * * TX_TSTAMP - access to request up to one transmit timestamp at a time
 * * RX_TSTAMP - access to request Rx timestamps on all received packets
 * * PHC_REGS - direct access to the clock time registers for reading PHC
 *
 * The PF will reply with the same opcode a filled out copy of the
 * virtchnl_ptp_caps structure which defines the specifics of which features
 * are accessible to this device.
 */
int iavf_send_vf_ptp_caps_msg(struct iavf_adapter *adapter)
{
	struct virtchnl_ptp_caps hw_caps = {};

	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_PTP_CAPS;

	if (!PTP_ALLOWED(adapter))
		return -EOPNOTSUPP;

	hw_caps.caps = (VIRTCHNL_1588_PTP_CAP_READ_PHC |
			VIRTCHNL_1588_PTP_CAP_WRITE_PHC |
			VIRTCHNL_1588_PTP_CAP_TX_TSTAMP |
			VIRTCHNL_1588_PTP_CAP_RX_TSTAMP |
			VIRTCHNL_1588_PTP_CAP_PHC_REGS);

	adapter->current_op = VIRTCHNL_OP_1588_PTP_GET_CAPS;

	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_1588_PTP_GET_CAPS,
				(u8 *)&hw_caps, sizeof(hw_caps));
}

/**
 * iavf_validate_num_queues
 * @adapter: adapter structure
 *
 * Validate that the number of queues the PF has sent in
 * VIRTCHNL_OP_GET_VF_RESOURCES is not larger than the VF can handle.
 **/
static void iavf_validate_num_queues(struct iavf_adapter *adapter)
{
	/* When ADQ is enabled PF allocates 16 queues to VF but enables only
	 * the specified number of queues it's been requested for (as per TC
	 * info). So this check should be skipped when ADQ is enabled.
	 */
	if (iavf_is_adq_enabled(adapter))
		return;

	if (adapter->vf_res->num_queue_pairs > IAVF_MAX_REQ_QUEUES) {
		struct virtchnl_vsi_resource *vsi_res;
		int i;

		dev_info(&adapter->pdev->dev, "Received %d queues, but can only have a max of %d\n",
			 adapter->vf_res->num_queue_pairs,
			 IAVF_MAX_REQ_QUEUES);
		dev_info(&adapter->pdev->dev, "Fixing by reducing queues to %d\n",
			 IAVF_MAX_REQ_QUEUES);
		adapter->vf_res->num_queue_pairs = IAVF_MAX_REQ_QUEUES;
		for (i = 0; i < adapter->vf_res->num_vsis; i++) {
			vsi_res = &adapter->vf_res->vsi_res[i];
			vsi_res->num_queue_pairs = IAVF_MAX_REQ_QUEUES;
		}
	}
}

/**
 * iavf_get_vf_config
 * @adapter: private adapter structure
 *
 * Get VF configuration from PF and populate hw structure. Must be called after
 * admin queue is initialized. Busy waits until response is received from PF,
 * with maximum timeout. Response from PF is returned in the buffer for further
 * processing by the caller.
 **/
int iavf_get_vf_config(struct iavf_adapter *adapter)
{
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;
	u16 len;

	len =  sizeof(struct virtchnl_vf_resource) +
		IAVF_MAX_VF_VSI * sizeof(struct virtchnl_vsi_resource);
	event.buf_len = len;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		err = iavf_clean_arq_element(hw, &event, NULL);
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_GET_VF_RESOURCES)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	memcpy(adapter->vf_res, event.msg_buf, min(event.msg_len, len));

	/* some PFs send more queues than we should have so validate that
	 * we aren't getting too many queues
	 */
	if (!err)
		iavf_validate_num_queues(adapter);
	iavf_vf_parse_hw_config(hw, adapter->vf_res);
out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

int iavf_get_vf_vlan_v2_caps(struct iavf_adapter *adapter)
{
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;
	u16 len;

	len =  sizeof(struct virtchnl_vlan_caps);
	event.buf_len = len;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		err = iavf_clean_arq_element(hw, &event, NULL);
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	if (err)
		goto out_alloc;

	memcpy(&adapter->vlan_v2_caps, event.msg_buf, min(event.msg_len, len));
out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

int iavf_get_vf_supported_rxdids(struct iavf_adapter *adapter)
{
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;
	u16 len;

	len =  sizeof(struct virtchnl_supported_rxdids);
	event.buf_len = len;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		err = iavf_clean_arq_element(hw, &event, NULL);
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_GET_SUPPORTED_RXDIDS)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	if (err)
		goto out_alloc;

	memcpy(&adapter->supported_rxdids, event.msg_buf, min(event.msg_len, len));
out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

int iavf_get_vf_ptp_caps(struct iavf_adapter *adapter)
{
	struct iavf_hw *hw = &adapter->hw;
	struct iavf_arq_event_info event;
	enum virtchnl_ops op;
	enum iavf_status err;
	u16 len;

	len =  sizeof(struct virtchnl_ptp_caps);
	event.buf_len = len;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf) {
		err = -ENOMEM;
		goto out;
	}

	while (1) {
		/* When the AQ is empty, iavf_clean_arq_element will return
		 * nonzero and this loop will terminate.
		 */
		err = iavf_clean_arq_element(hw, &event, NULL);
		if (err)
			goto out_alloc;
		op =
		    (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		if (op == VIRTCHNL_OP_1588_PTP_GET_CAPS)
			break;
	}

	err = (enum iavf_status)le32_to_cpu(event.desc.cookie_low);
	if (err)
		goto out_alloc;

	memcpy(&adapter->ptp.hw_caps, event.msg_buf, min(event.msg_len, len));
out_alloc:
	kfree(event.msg_buf);
out:
	return err;
}

/**
 * iavf_configure_queues
 * @adapter: adapter structure
 *
 * Request that the PF set up our (previously allocated) queues.
 **/
void iavf_configure_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_vsi_queue_config_info *vqci;
	struct virtchnl_queue_pair_info *vqpi;
	int pairs = adapter->num_active_queues;
	int i, len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_CONFIG_VSI_QUEUES;
	len = sizeof(struct virtchnl_vsi_queue_config_info) +
		       (sizeof(struct virtchnl_queue_pair_info) * pairs);
	vqci = kzalloc(len, GFP_KERNEL);
	if (!vqci)
		return;

	vqci->vsi_id = adapter->vsi_res->vsi_id;
	vqci->num_queue_pairs = pairs;
	vqpi = vqci->qpair;
	/* Size check is not needed here - HW max is 16 queue pairs, and we
	 * can fit info for 31 of them into the AQ buffer before it overflows.
	 */
	for (i = 0; i < pairs; i++) {
		vqpi->txq.vsi_id = vqci->vsi_id;
		vqpi->txq.queue_id = i;
		vqpi->txq.ring_len = adapter->tx_rings[i].count;
		vqpi->txq.dma_ring_addr = adapter->tx_rings[i].dma;
		vqpi->rxq.vsi_id = vqci->vsi_id;
		vqpi->rxq.queue_id = i;
		vqpi->rxq.ring_len = adapter->rx_rings[i].count;
		vqpi->rxq.dma_ring_addr = adapter->rx_rings[i].dma;
		vqpi->rxq.max_pkt_size = adapter->netdev->mtu +
					 IAVF_PACKET_HDR_PAD;
		vqpi->rxq.databuffer_size =
			ALIGN(adapter->rx_rings[i].rx_buf_len,
			      BIT_ULL(IAVF_RXQ_CTX_DBUFF_SHIFT));
		if (RXDID_ALLOWED(adapter))
			vqpi->rxq.rxdid = adapter->rxdid;
		vqpi++;
	}

	adapter->aq_required &= ~IAVF_FLAG_AQ_CONFIGURE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
			 (u8 *)vqci, len);
	kfree(vqci);
}

/**
 * iavf_enable_queues
 * @adapter: adapter structure
 *
 * Request that the PF enable all of our queues.
 **/
void iavf_enable_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot enable queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ENABLE_QUEUES;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	vqs.tx_queues = BIT(adapter->num_active_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_QUEUES,
			 (u8 *)&vqs, sizeof(vqs));
}

/**
 * iavf_disable_queues
 * @adapter: adapter structure
 *
 * Request that the PF disable all of our queues.
 **/
void iavf_disable_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot disable queues, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DISABLE_QUEUES;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	vqs.tx_queues = BIT(adapter->num_active_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_QUEUES;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_QUEUES,
			 (u8 *)&vqs, sizeof(vqs));
}

/**
 * iavf_map_queues
 * @adapter: adapter structure
 *
 * Request that the PF map queues to interrupt vectors. Misc causes, including
 * admin queue, are always mapped to vector 0.
 **/
void iavf_map_queues(struct iavf_adapter *adapter)
{
	struct virtchnl_irq_map_info *vimi;
	struct virtchnl_vector_map *vecmap;
	int v_idx, q_vectors, len;
	struct iavf_q_vector *q_vector;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot map queues to vectors, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_CONFIG_IRQ_MAP;

	q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	len = sizeof(struct virtchnl_irq_map_info) +
	      (adapter->num_msix_vectors *
		sizeof(struct virtchnl_vector_map));
	vimi = kzalloc(len, GFP_KERNEL);
	if (!vimi)
		return;

	vimi->num_vectors = adapter->num_msix_vectors;
	/* Queue vectors first */
	for (v_idx = 0; v_idx < q_vectors; v_idx++) {
		q_vector = &adapter->q_vectors[v_idx];
		vecmap = &vimi->vecmap[v_idx];

		vecmap->vsi_id = adapter->vsi_res->vsi_id;
		vecmap->vector_id = v_idx + NONQ_VECS;
		vecmap->txq_map = q_vector->ring_mask;
		vecmap->rxq_map = q_vector->ring_mask;
		vecmap->rxitr_idx = IAVF_RX_ITR;
		vecmap->txitr_idx = IAVF_TX_ITR;
	}
	/* Misc vector last - this is only for AdminQ messages */
	vecmap = &vimi->vecmap[v_idx];
	vecmap->vsi_id = adapter->vsi_res->vsi_id;
	vecmap->vector_id = 0;
	vecmap->txq_map = 0;
	vecmap->rxq_map = 0;

	adapter->aq_required &= ~IAVF_FLAG_AQ_MAP_VECTORS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_IRQ_MAP,
			 (u8 *)vimi, len);
	kfree(vimi);
}

/**
 * iavf_request_queues
 * @adapter: adapter structure
 * @num: number of requested queues
 *
 * We get a default number of queues from the PF.  This enables us to request a
 * different number.  Returns 0 on success, negative on failure
 **/
int iavf_request_queues(struct iavf_adapter *adapter, int num)
{
	struct virtchnl_vf_res_request vfres;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot request queues, command %d pending\n",
			adapter->current_op);
		return -EBUSY;
	}

	vfres.num_queue_pairs = min_t(int, num, num_online_cpus());

	adapter->current_op = VIRTCHNL_OP_REQUEST_QUEUES;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	return iavf_send_pf_msg(adapter, VIRTCHNL_OP_REQUEST_QUEUES,
				(u8 *)&vfres, sizeof(vfres));
}

/**
 * iavf_set_mac_addr_type
 * @virtchnl_ether_addr: pointer to request list element
 * @filter: pointer filter being requested
 *
 * Set the correct request type.
 **/
static void
iavf_set_mac_addr_type(struct virtchnl_ether_addr *virtchnl_ether_addr,
		       struct iavf_mac_filter *filter)
{
	virtchnl_ether_addr->type = filter->is_primary ?
		VIRTCHNL_ETHER_ADDR_PRIMARY :
		VIRTCHNL_ETHER_ADDR_EXTRA;
}

/**
 * iavf_add_ether_addrs
 * @adapter: adapter structure
 *
 * Request that the PF add one or more addresses to our filters.
 **/
void iavf_add_ether_addrs(struct iavf_adapter *adapter)
{
	struct virtchnl_ether_addr_list *veal;
	int len, i = 0, count = 0;
	struct iavf_mac_filter *f;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add filters, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->add)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_MAC_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_ETH_ADDR;

	len = sizeof(struct virtchnl_ether_addr_list) +
	      (count * sizeof(struct virtchnl_ether_addr));
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many add MAC changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_ether_addr_list)) /
			sizeof(struct virtchnl_ether_addr);
		len = sizeof(struct virtchnl_ether_addr_list) +
		      (count * sizeof(struct virtchnl_ether_addr));
		more = true;
	}

	veal = kzalloc(len, GFP_ATOMIC);
	if (!veal) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	veal->vsi_id = adapter->vsi_res->vsi_id;
	veal->num_elements = count;
	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->add) {
			ether_addr_copy(veal->list[i].addr, f->macaddr);
			iavf_set_mac_addr_type(&veal->list[i], f);
			i++;
			f->add = false;
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_MAC_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_ETH_ADDR,
			 (u8 *)veal, len);
	kfree(veal);
}

/**
 * iavf_del_ether_addrs
 * @adapter: adapter structure
 *
 * Request that the PF remove one or more addresses from our filters.
 **/
void iavf_del_ether_addrs(struct iavf_adapter *adapter)
{
	struct virtchnl_ether_addr_list *veal;
	struct iavf_mac_filter *f, *ftmp;
	int len, i = 0, count = 0;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove filters, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (f->remove)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_MAC_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DEL_ETH_ADDR;

	len = sizeof(struct virtchnl_ether_addr_list) +
	      (count * sizeof(struct virtchnl_ether_addr));
	if (len > IAVF_MAX_AQ_BUF_SIZE) {
		dev_warn(&adapter->pdev->dev, "Too many delete MAC changes in one request\n");
		count = (IAVF_MAX_AQ_BUF_SIZE -
			 sizeof(struct virtchnl_ether_addr_list)) /
			sizeof(struct virtchnl_ether_addr);
		len = sizeof(struct virtchnl_ether_addr_list) +
		      (count * sizeof(struct virtchnl_ether_addr));
		more = true;
	}
	veal = kzalloc(len, GFP_ATOMIC);
	if (!veal) {
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	veal->vsi_id = adapter->vsi_res->vsi_id;
	veal->num_elements = count;
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		if (f->remove) {
			ether_addr_copy(veal->list[i].addr, f->macaddr);
			iavf_set_mac_addr_type(&veal->list[i], f);
			i++;
			list_del(&f->list);
			kfree(f);
			if (i == count)
				break;
		}
	}
	if (!more)
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_MAC_FILTER;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_ETH_ADDR,
			 (u8 *)veal, len);
	kfree(veal);
}

/**
 * iavf_mac_add_ok
 * @adapter: adapter structure
 *
 * Submit list of filters based on PF response.
 **/
static void iavf_mac_add_ok(struct iavf_adapter *adapter)
{
	struct iavf_mac_filter *f, *ftmp;

	spin_lock_bh(&adapter->mac_vlan_list_lock);
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		f->is_new_mac = false;
	}
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * iavf_mac_add_reject
 * @adapter: adapter structure
 *
 * Remove filters from list based on PF response.
 **/
static void iavf_mac_add_reject(struct iavf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct iavf_mac_filter *f, *ftmp;

	spin_lock_bh(&adapter->mac_vlan_list_lock);
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		if (f->remove && ether_addr_equal(f->macaddr, netdev->dev_addr))
			f->remove = false;

		if (f->is_new_mac) {
			list_del(&f->list);
			kfree(f);
		}
	}
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * iavf_add_vlans
 * @adapter: adapter structure
 *
 * Request that the PF add one or more VLAN filters to our VSI.
 **/
void iavf_add_vlans(struct iavf_adapter *adapter)
{
	int len, i = 0, count = 0;
	struct iavf_vlan_filter *f;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add VLANs, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (f->add)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_VLAN_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	if (VLAN_ALLOWED(adapter)) {
		struct virtchnl_vlan_filter_list *vvfl;

		adapter->current_op = VIRTCHNL_OP_ADD_VLAN;

		len = sizeof(*vvfl) + (count * sizeof(u16));
		if (len > IAVF_MAX_AQ_BUF_SIZE) {
			dev_warn(&adapter->pdev->dev, "Too many add VLAN changes in one request\n");
			count = (IAVF_MAX_AQ_BUF_SIZE - sizeof(*vvfl)) /
				sizeof(u16);
			len = sizeof(*vvfl) + (count * sizeof(u16));
			more = true;
		}
		vvfl = kzalloc(len, GFP_ATOMIC);
		if (!vvfl) {
			spin_unlock_bh(&adapter->mac_vlan_list_lock);
			return;
		}

		vvfl->vsi_id = adapter->vsi_res->vsi_id;
		vvfl->num_elements = count;
		list_for_each_entry(f, &adapter->vlan_filter_list, list) {
			if (f->add) {
				vvfl->vlan_id[i] = f->vlan.vid;
				i++;
				f->add = false;
				if (i == count)
					break;
			}
		}
		if (!more)
			adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_VLAN_FILTER;

		spin_unlock_bh(&adapter->mac_vlan_list_lock);

		iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_VLAN, (u8 *)vvfl, len);
		kfree(vvfl);
	} else if (VLAN_V2_ALLOWED(adapter)) {
		struct virtchnl_vlan_filter_list_v2 *vvfl_v2;

		adapter->current_op = VIRTCHNL_OP_ADD_VLAN_V2;

		len = sizeof(*vvfl_v2) + ((count - 1) *
					  sizeof(struct virtchnl_vlan_filter));
		if (len > IAVF_MAX_AQ_BUF_SIZE) {
			dev_warn(&adapter->pdev->dev, "Too many add VLAN changes in one request\n");
			count = (IAVF_MAX_AQ_BUF_SIZE - sizeof(*vvfl_v2)) /
				sizeof(struct virtchnl_vlan_filter);
			len = sizeof(*vvfl_v2) +
				((count - 1) *
				 sizeof(struct virtchnl_vlan_filter));
			more = true;
		}

		vvfl_v2 = kzalloc(len, GFP_ATOMIC);
		if (!vvfl_v2) {
			spin_unlock_bh(&adapter->mac_vlan_list_lock);
			return;
		}

		vvfl_v2->vport_id = adapter->vsi_res->vsi_id;
		vvfl_v2->num_elements = count;
		list_for_each_entry(f, &adapter->vlan_filter_list, list) {
			if (f->add) {
				struct virtchnl_vlan_supported_caps *filtering_support =
					&adapter->vlan_v2_caps.filtering.filtering_support;
				struct virtchnl_vlan *vlan;

				/* give priority over outer if it's enabled */
				if (filtering_support->outer)
					vlan = &vvfl_v2->filters[i].outer;
				else
					vlan = &vvfl_v2->filters[i].inner;

				vlan->tci = f->vlan.vid;
				vlan->tpid = f->vlan.tpid;

				i++;
				f->add = false;
				if (i == count)
					break;
			}
		}

		if (!more)
			adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_VLAN_FILTER;

		spin_unlock_bh(&adapter->mac_vlan_list_lock);

		iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_VLAN_V2,
				 (u8 *)vvfl_v2, len);
		kfree(vvfl_v2);
	}
}

/**
 * iavf_del_vlans
 * @adapter: adapter structure
 *
 * Request that the PF remove one or more VLAN filters from our VSI.
 **/
void iavf_del_vlans(struct iavf_adapter *adapter)
{
	struct iavf_vlan_filter *f, *ftmp;
	int len, i = 0, count = 0;
	bool more = false;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove VLANs, command %d pending\n",
			adapter->current_op);
		return;
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (f->remove)
			count++;
	}
	if (!count) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_VLAN_FILTER;
		spin_unlock_bh(&adapter->mac_vlan_list_lock);
		return;
	}

	if (VLAN_ALLOWED(adapter)) {
		struct virtchnl_vlan_filter_list *vvfl;

		adapter->current_op = VIRTCHNL_OP_DEL_VLAN;

		len = sizeof(*vvfl) + (count * sizeof(u16));
		if (len > IAVF_MAX_AQ_BUF_SIZE) {
			dev_warn(&adapter->pdev->dev, "Too many delete VLAN changes in one request\n");
			count = (IAVF_MAX_AQ_BUF_SIZE - sizeof(*vvfl)) /
				sizeof(u16);
			len = sizeof(*vvfl) + (count * sizeof(u16));
			more = true;
		}
		vvfl = kzalloc(len, GFP_ATOMIC);
		if (!vvfl) {
			spin_unlock_bh(&adapter->mac_vlan_list_lock);
			return;
		}

		vvfl->vsi_id = adapter->vsi_res->vsi_id;
		vvfl->num_elements = count;
		list_for_each_entry_safe(f, ftmp, &adapter->vlan_filter_list, list) {
			if (f->remove) {
				vvfl->vlan_id[i] = f->vlan.vid;
				i++;
				list_del(&f->list);
				kfree(f);
				if (i == count)
					break;
			}
		}

		if (!more)
			adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_VLAN_FILTER;

		spin_unlock_bh(&adapter->mac_vlan_list_lock);

		iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_VLAN, (u8 *)vvfl, len);
		kfree(vvfl);
	} else if (VLAN_V2_ALLOWED(adapter)) {
		struct virtchnl_vlan_filter_list_v2 *vvfl_v2;

		adapter->current_op = VIRTCHNL_OP_DEL_VLAN_V2;

		len = sizeof(*vvfl_v2) +
			((count - 1) * sizeof(struct virtchnl_vlan_filter));
		if (len > IAVF_MAX_AQ_BUF_SIZE) {
			dev_warn(&adapter->pdev->dev, "Too many add VLAN changes in one request\n");
			count = (IAVF_MAX_AQ_BUF_SIZE -
				 sizeof(*vvfl_v2)) /
				sizeof(struct virtchnl_vlan_filter);
			len = sizeof(*vvfl_v2) +
				((count - 1) *
				 sizeof(struct virtchnl_vlan_filter));
			more = true;
		}

		vvfl_v2 = kzalloc(len, GFP_ATOMIC);
		if (!vvfl_v2) {
			spin_unlock_bh(&adapter->mac_vlan_list_lock);
			return;
		}

		vvfl_v2->vport_id = adapter->vsi_res->vsi_id;
		vvfl_v2->num_elements = count;
		list_for_each_entry_safe(f, ftmp, &adapter->vlan_filter_list, list) {
			if (f->remove) {
				struct virtchnl_vlan_supported_caps *filtering_support =
					&adapter->vlan_v2_caps.filtering.filtering_support;
				struct virtchnl_vlan *vlan;

				/* give priority over outer if it's enabled */
				if (filtering_support->outer)
					vlan = &vvfl_v2->filters[i].outer;
				else
					vlan = &vvfl_v2->filters[i].inner;

				vlan->tci = f->vlan.vid;
				vlan->tpid = f->vlan.tpid;

				list_del(&f->list);
				kfree(f);
				i++;
				if (i == count)
					break;
			}
		}

		if (!more)
			adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_VLAN_FILTER;

		spin_unlock_bh(&adapter->mac_vlan_list_lock);

		iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_VLAN_V2,
				 (u8 *)vvfl_v2, len);
		kfree(vvfl_v2);
	}
}

/**
 * iavf_set_promiscuous
 * @adapter: adapter structure
 *
 * Request that the PF enable promiscuous mode for our VSI.
 **/
void iavf_set_promiscuous(struct iavf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct virtchnl_promisc_info vpi;
	unsigned int flags;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev,
			"Cannot set promiscuous mode, command %d pending\n",
			adapter->current_op);
		return;
	}

	/* prevent changes to promiscuous flags */
	spin_lock_bh(&adapter->current_netdev_promisc_flags_lock);

	/* sanity check to prevent duplicate AQ calls */
	if (!iavf_promiscuous_mode_changed(adapter)) {
		adapter->aq_required &= ~IAVF_FLAG_AQ_CONFIGURE_PROMISC_MODE;
		dev_dbg(&adapter->pdev->dev, "No change in promiscuous mode\n");
		/* allow changes to promiscuous flags */
		spin_unlock_bh(&adapter->current_netdev_promisc_flags_lock);
		return;
	}

	/* there are 2 bits, but only 3 states */
	if (!(netdev->flags & IFF_PROMISC) &&
	    netdev->flags & IFF_ALLMULTI) {
		/* State 1  - only multicast promiscuous mode enabled
		 * - !IFF_PROMISC && IFF_ALLMULTI
		 */
		flags = FLAG_VF_MULTICAST_PROMISC;
		adapter->current_netdev_promisc_flags |= IFF_ALLMULTI;
		adapter->current_netdev_promisc_flags &= ~IFF_PROMISC;
		dev_info(&adapter->pdev->dev,
			 "Entering multicast promiscuous mode\n");
	} else if (!(netdev->flags & IFF_PROMISC) &&
		   !(netdev->flags & IFF_ALLMULTI)) {
		/* State 2 - unicast/multicast promiscuous mode disabled
		 * - !IFF_PROMISC && !IFF_ALLMULTI
		 */
		flags = 0;
		adapter->current_netdev_promisc_flags &=
			~(IFF_PROMISC | IFF_ALLMULTI);
		dev_info(&adapter->pdev->dev, "Leaving promiscuous mode\n");
	} else {
		/* State 3 - unicast/multicast promiscuous mode enabled
		 * - IFF_PROMISC && IFF_ALLMULTI
		 * - IFF_PROMISC && !IFF_ALLMULTI
		 */
		flags = FLAG_VF_UNICAST_PROMISC | FLAG_VF_MULTICAST_PROMISC;
		adapter->current_netdev_promisc_flags |= IFF_PROMISC;
		if (netdev->flags & IFF_ALLMULTI)
			adapter->current_netdev_promisc_flags |= IFF_ALLMULTI;
		else
			adapter->current_netdev_promisc_flags &= ~IFF_ALLMULTI;

		dev_info(&adapter->pdev->dev, "Entering promiscuous mode\n");
	}

	adapter->aq_required &= ~IAVF_FLAG_AQ_CONFIGURE_PROMISC_MODE;

	/* allow changes to promiscuous flags */
	spin_unlock_bh(&adapter->current_netdev_promisc_flags_lock);

	adapter->current_op = VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE;
	vpi.vsi_id = adapter->vsi_res->vsi_id;
	vpi.flags = flags;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
			 (u8 *)&vpi, sizeof(vpi));
}

/**
 * iavf_request_stats
 * @adapter: adapter structure
 *
 * Request VSI statistics from PF.
 **/
void iavf_request_stats(struct iavf_adapter *adapter)
{
	struct virtchnl_queue_select vqs;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* no error message, this isn't crucial */
		return;
	}

	adapter->aq_required &= ~IAVF_FLAG_AQ_REQUEST_STATS;
	adapter->current_op = VIRTCHNL_OP_GET_STATS;
	vqs.vsi_id = adapter->vsi_res->vsi_id;
	/* queue maps are ignored for this message - only the vsi is used */
	if (iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_STATS,
			     (u8 *)&vqs, sizeof(vqs)))
		/* if the request failed, don't lock out others */
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;
}

/**
 * iavf_get_hena
 * @adapter: adapter structure
 *
 * Request hash enable capabilities from PF
 **/
void iavf_get_hena(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot get RSS hash capabilities, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_GET_RSS_HENA_CAPS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_GET_HENA;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_GET_RSS_HENA_CAPS, NULL, 0);
}

/**
 * iavf_set_hena
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS hash capabilities
 **/
void iavf_set_hena(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_hena vrh;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS hash enable, command %d pending\n",
			adapter->current_op);
		return;
	}
	vrh.hena = adapter->hena;
	adapter->current_op = VIRTCHNL_OP_SET_RSS_HENA;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_HENA;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_SET_RSS_HENA, (u8 *)&vrh,
			 sizeof(vrh));
}

/**
 * iavf_set_rss_key
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS hash key
 **/
void iavf_set_rss_key(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_key *vrk;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS key, command %d pending\n",
			adapter->current_op);
		return;
	}
	len = sizeof(struct virtchnl_rss_key) +
	      (adapter->rss_key_size * sizeof(u8)) - 1;
	vrk = kzalloc(len, GFP_KERNEL);
	if (!vrk)
		return;
	vrk->vsi_id = adapter->vsi.id;
	vrk->key_len = adapter->rss_key_size;
	memcpy(vrk->key, adapter->rss_key, adapter->rss_key_size);

	adapter->current_op = VIRTCHNL_OP_CONFIG_RSS_KEY;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_RSS_KEY;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_RSS_KEY, (u8 *)vrk, len);
	kfree(vrk);
}

/**
 * iavf_set_rss_lut
 * @adapter: adapter structure
 *
 * Request the PF to set our RSS lookup table
 **/
void iavf_set_rss_lut(struct iavf_adapter *adapter)
{
	struct virtchnl_rss_lut *vrl;
	int len;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot set RSS LUT, command %d pending\n",
			adapter->current_op);
		return;
	}
	len = sizeof(struct virtchnl_rss_lut) +
	      (adapter->rss_lut_size * sizeof(u8)) - 1;
	vrl = kzalloc(len, GFP_KERNEL);
	if (!vrl)
		return;
	vrl->vsi_id = adapter->vsi.id;
	vrl->lut_entries = adapter->rss_lut_size;
	memcpy(vrl->lut, adapter->rss_lut, adapter->rss_lut_size);
	adapter->current_op = VIRTCHNL_OP_CONFIG_RSS_LUT;
	adapter->aq_required &= ~IAVF_FLAG_AQ_SET_RSS_LUT;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_CONFIG_RSS_LUT, (u8 *)vrl, len);
	kfree(vrl);
}

/**
 * iavf_enable_vlan_stripping
 * @adapter: adapter structure
 *
 * Request VLAN header stripping to be enabled
 **/
void iavf_enable_vlan_stripping(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot enable stripping, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ENABLE_VLAN_STRIPPING;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_VLAN_STRIPPING;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING, NULL, 0);
}

/**
 * iavf_disable_vlan_stripping
 * @adapter: adapter structure
 *
 * Request VLAN header stripping to be disabled
 **/
void iavf_disable_vlan_stripping(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot disable stripping, command %d pending\n",
			adapter->current_op);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DISABLE_VLAN_STRIPPING;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_VLAN_STRIPPING;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING, NULL, 0);
}

/**
 * iavf_tpid_to_vc_ethertype - transform from VLAN TPID to virtchnl ethertype
 * @tpid: VLAN TPID (i.e. 0x8100, 0x88a8, etc.)
 */
static u32 iavf_tpid_to_vc_ethertype(u16 tpid)
{
	switch (tpid) {
	case ETH_P_8021Q:
		return VIRTCHNL_VLAN_ETHERTYPE_8100;
	case ETH_P_8021AD:
		return VIRTCHNL_VLAN_ETHERTYPE_88A8;
	}

	return 0;
}

/**
 * iavf_set_vc_offload_ethertype - set virtchnl ethertype for offload message
 * @adapter: adapter structure
 * @msg: message structure used for updating offloads over virtchnl to update
 * @tpid: VLAN TPID (i.e. 0x8100, 0x88a8, etc.)
 * @offload_op: opcode used to determine which support structure to check
 */
static int
iavf_set_vc_offload_ethertype(struct iavf_adapter *adapter,
			      struct virtchnl_vlan_setting *msg, u16 tpid,
			      enum virtchnl_ops offload_op)
{
	struct virtchnl_vlan_supported_caps *offload_support;
	u32 vc_ethertype = iavf_tpid_to_vc_ethertype(tpid);

	/* reference the correct offload support structure */
	switch (offload_op) {
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
		/* fall-through */
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
		offload_support =
			&adapter->vlan_v2_caps.offloads.stripping_support;
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
		/* fall-through */
	case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
		offload_support =
			&adapter->vlan_v2_caps.offloads.insertion_support;
		break;
	default:
		dev_err(&adapter->pdev->dev, "Invalid opcode %d for setting virtchnl ethertype to enable/disable VLAN offloads\n",
			offload_op);
		return -EINVAL;
	}

	/* make sure ethertype is supported */
	if ((offload_support->outer & vc_ethertype) &&
	    (offload_support->outer & VIRTCHNL_VLAN_TOGGLE)) {
		msg->outer_ethertype_setting = vc_ethertype;
	} else if ((offload_support->inner & vc_ethertype) &&
		   (offload_support->inner & VIRTCHNL_VLAN_TOGGLE)) {
		msg->inner_ethertype_setting = vc_ethertype;
	} else {
		dev_dbg(&adapter->pdev->dev, "opcode %d unsupported for VLAN TPID 0x%04x\n",
			offload_op, tpid);
		return -EINVAL;
	}

	return 0;
}

/**
 * iavf_clear_offload_v2_aq_required - clear AQ required bit for offload request
 * @adapter: adapter structure
 * @tpid: VLAN TPID
 * @offload_op: opcode used to determine which AQ required bit to clear
 */
static void
iavf_clear_offload_v2_aq_required(struct iavf_adapter *adapter, u16 tpid,
				  enum virtchnl_ops offload_op)
{
	switch (offload_op) {
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
		if (tpid == ETH_P_8021Q)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_ENABLE_CTAG_VLAN_STRIPPING;
		else if (tpid == ETH_P_8021AD)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_ENABLE_STAG_VLAN_STRIPPING;
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
		if (tpid == ETH_P_8021Q)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_DISABLE_CTAG_VLAN_STRIPPING;
		else if (tpid == ETH_P_8021AD)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_DISABLE_STAG_VLAN_STRIPPING;
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
		if (tpid == ETH_P_8021Q)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_ENABLE_CTAG_VLAN_INSERTION;
		else if (tpid == ETH_P_8021AD)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_ENABLE_STAG_VLAN_INSERTION;
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
		if (tpid == ETH_P_8021Q)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_DISABLE_CTAG_VLAN_INSERTION;
		else if (tpid == ETH_P_8021AD)
			adapter->aq_required &=
				~IAVF_FLAG_AQ_DISABLE_STAG_VLAN_INSERTION;
		break;
	default:
		dev_err(&adapter->pdev->dev, "Unsupported opcode %d specified for clearing aq_required bits for VIRTCHNL_VF_OFFLOAD_VLAN_V2 offload request\n",
			offload_op);
	}
}

/**
 * iavf_send_vlan_offload_v2 - send offload enable/disable over virtchnl
 * @adapter: adapter structure
 * @tpid: VLAN TPID used for the command (i.e. 0x8100 or 0x88a8)
 * @offload_op: offload_op used to make the request over virtchnl
 */
static void
iavf_send_vlan_offload_v2(struct iavf_adapter *adapter, u16 tpid,
			  enum virtchnl_ops offload_op)
{
	struct virtchnl_vlan_setting *msg;
	int len = sizeof(*msg);

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot send %d, command %d pending\n",
			offload_op, adapter->current_op);
		return;
	}

	adapter->current_op = offload_op;

	msg = kzalloc(len, GFP_KERNEL);
	if (!msg)
		return;

	msg->vport_id = adapter->vsi_res->vsi_id;

	/* always clear to prevent unsupported and endless requests */
	iavf_clear_offload_v2_aq_required(adapter, tpid, offload_op);

	/* only send valid offload requests */
	if (!iavf_set_vc_offload_ethertype(adapter, msg, tpid, offload_op))
		iavf_send_pf_msg(adapter, offload_op, (u8 *)msg, len);
	else
		/* since the current_op assigned in this function was never sent
		 * there will never be a completion to clear it, so do that now
		 * to allow other opcodes
		 */
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;

	kfree(msg);
}

/**
 * iavf_enable_vlan_stripping_v2 - enable VLAN stripping
 * @adapter: adapter structure
 * @tpid: VLAN TPID used to enable VLAN stripping
 */
void iavf_enable_vlan_stripping_v2(struct iavf_adapter *adapter, u16 tpid)
{
	iavf_send_vlan_offload_v2(adapter, tpid,
				  VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2);
}

/**
 * iavf_disable_vlan_stripping_v2 - disable VLAN stripping
 * @adapter: adapter structure
 * @tpid: VLAN TPID used to disable VLAN stripping
 */
void iavf_disable_vlan_stripping_v2(struct iavf_adapter *adapter, u16 tpid)
{
	iavf_send_vlan_offload_v2(adapter, tpid,
				  VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2);
}

/**
 * iavf_enable_vlan_insertion_v2 - enable VLAN insertion
 * @adapter: adapter structure
 * @tpid: VLAN TPID used to enable VLAN insertion
 */
void iavf_enable_vlan_insertion_v2(struct iavf_adapter *adapter, u16 tpid)
{
	iavf_send_vlan_offload_v2(adapter, tpid,
				  VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2);
}

/**
 * iavf_disable_vlan_insertion_v2 - disable VLAN insertion
 * @adapter: adapter structure
 * @tpid: VLAN TPID used to disable VLAN insertion
 */
void iavf_disable_vlan_insertion_v2(struct iavf_adapter *adapter, u16 tpid)
{
	iavf_send_vlan_offload_v2(adapter, tpid,
				  VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2);
}

/**
 * iavf_virtchnl_send_ptp_cmd - Send one queued PTP command
 * @adapter: adapter private structure
 *
 * De-queue one PTP command request and send the command message to the PF.
 * Clear IAVF_FLAG_AQ_SEND_PTP_CMD if no more messages are left to send.
 */
void iavf_virtchnl_send_ptp_cmd(struct iavf_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	struct iavf_ptp_aq_cmd *cmd;
	int err;

	if (WARN_ON(!adapter->ptp.initialized)) {
		/* This shouldn't be possible to hit, since no messages should
		 * be queued if PTP is not initialized.
		 */
		adapter->aq_required &= ~IAVF_FLAG_AQ_SEND_PTP_CMD;
		return;
	}

	spin_lock(&adapter->ptp.aq_cmd_lock);
	cmd = list_first_entry_or_null(&adapter->ptp.aq_cmds, struct iavf_ptp_aq_cmd, list);
	if (!cmd) {
		/* no further PTP messages to send */
		adapter->aq_required &= ~IAVF_FLAG_AQ_SEND_PTP_CMD;
		goto out_unlock;
	}

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(dev, "Cannot send PTP command %d, command %d pending\n",
			cmd->v_opcode, adapter->current_op);
		goto out_unlock;
	}

	err = iavf_send_pf_msg(adapter, cmd->v_opcode, cmd->msg, cmd->msglen);
	if (!err) {
		/* Command was sent without errors, so we can remove it from
		 * the list and discard it.
		 */
		list_del(&cmd->list);
		kfree(cmd);
	} else {
		/* We failed to send the command, try again next cycle */
		dev_warn(dev, "Failed to send PTP command %d\n", cmd->v_opcode);
	}

	if (list_empty(&adapter->ptp.aq_cmds))
		/* no further PTP messages to send */
		adapter->aq_required &= ~IAVF_FLAG_AQ_SEND_PTP_CMD;

out_unlock:
	spin_unlock(&adapter->ptp.aq_cmd_lock);
}

#define IAVF_MAX_SPEED_STRLEN        13

/**
 * iavf_print_link_message - print link up or down
 * @adapter: adapter structure
 *
 * Log a message telling the world of our wonderous link status
 */
static void iavf_print_link_message(struct iavf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int link_speed_mbps;
	char *speed;

	if (!adapter->link_up) {
		netdev_info(netdev, "NIC Link is Down\n");
		return;
	}

	speed = kcalloc(1, IAVF_MAX_SPEED_STRLEN, GFP_KERNEL);
	if (!speed)
		return;

#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
	if (ADV_LINK_SUPPORT(adapter)) {
		link_speed_mbps = adapter->link_speed_mbps;
		goto print_link_msg;
	}

#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */
	switch (adapter->link_speed) {
	case VIRTCHNL_LINK_SPEED_40GB:
		link_speed_mbps = SPEED_40000;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
		link_speed_mbps = SPEED_25000;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		link_speed_mbps = SPEED_20000;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		link_speed_mbps = SPEED_10000;
		break;
	case VIRTCHNL_LINK_SPEED_5GB:
		link_speed_mbps = SPEED_5000;
		break;
	case VIRTCHNL_LINK_SPEED_2_5GB:
		link_speed_mbps = SPEED_2500;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		link_speed_mbps = SPEED_1000;
		break;
	case VIRTCHNL_LINK_SPEED_100MB:
		link_speed_mbps = SPEED_100;
		break;
	default:
		link_speed_mbps = SPEED_UNKNOWN;
		break;
	}

#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
print_link_msg:
#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */
	if (link_speed_mbps > SPEED_1000) {
		if (link_speed_mbps == SPEED_2500)
			snprintf(speed, IAVF_MAX_SPEED_STRLEN, "2.5 Gbps");
		else
		/* convert to Gbps inline */
			snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%d %s",
				 link_speed_mbps / 1000, "Gbps");
	} else if (link_speed_mbps == SPEED_UNKNOWN)
		snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%s", "Unknown Mbps");
	else
		snprintf(speed, IAVF_MAX_SPEED_STRLEN, "%d %s",
			 link_speed_mbps, "Mbps");

	netdev_info(netdev, "NIC Link is Up Speed is %s Full Duplex\n", speed);
#ifndef SPEED_25000
	netdev_info(netdev, "Ethtool won't report 25 Gbps Link Speed correctly on this Kernel, Time for an Upgrade\n");
#endif
	kfree(speed);
}

#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
/**
 * iavf_get_vpe_link_status
 * @adapter: adapter structure
 * @vpe: virtchnl_pf_event structure
 *
 * Helper function for determining the link status
 **/
static bool
iavf_get_vpe_link_status(struct iavf_adapter *adapter,
			   struct virtchnl_pf_event *vpe)
{
	if (ADV_LINK_SUPPORT(adapter))
		return vpe->event_data.link_event_adv.link_status;
	else
		return vpe->event_data.link_event.link_status;
}

/**
 * iavf_set_adapter_link_speed_from_vpe
 * @adapter: adapter structure for which we are setting the link speed
 * @vpe: virtchnl_pf_event structure that contains the link speed we are setting
 *
 * Helper function for setting iavf_adapter link speed
 **/
static void
iavf_set_adapter_link_speed_from_vpe(struct iavf_adapter *adapter,
				       struct virtchnl_pf_event *vpe)
{
	if (ADV_LINK_SUPPORT(adapter))
		adapter->link_speed_mbps =
			vpe->event_data.link_event_adv.link_speed;
	else
		adapter->link_speed = vpe->event_data.link_event.link_speed;
}

#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */
/**
 * iavf_enable_channels
 * @adapter: adapter structure
 *
 * Request that the PF enable channels as specified by
 * the user via tc tool.
 **/
void iavf_enable_channels(struct iavf_adapter *adapter)
{
	struct virtchnl_tc_info *vti = NULL;
	u16 len;
	int i;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure mqprio, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = ((adapter->num_tc - 1) * sizeof(struct virtchnl_channel_info)) +
	       sizeof(struct virtchnl_tc_info);

	vti = kzalloc(len, GFP_KERNEL);
	if (!vti)
		return;
	vti->num_tc = adapter->num_tc;
	for (i = 0; i < vti->num_tc; i++) {
		vti->list[i].count = adapter->ch_config.ch_info[i].count;
		vti->list[i].offset = adapter->ch_config.ch_info[i].offset;
		vti->list[i].pad = 0;
		vti->list[i].max_tx_rate =
				adapter->ch_config.ch_info[i].max_tx_rate;
	}

	adapter->ch_config.state = __IAVF_TC_RUNNING;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	adapter->current_op = VIRTCHNL_OP_ENABLE_CHANNELS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_ENABLE_CHANNELS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ENABLE_CHANNELS, (u8 *)vti, len);
	kfree(vti);
}

/**
 * iavf_disable_channels
 * @adapter: adapter structure
 *
 * Request that the PF disable channels that are configured
 **/
void iavf_disable_channels(struct iavf_adapter *adapter)
{
	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot configure mqprio, command %d pending\n",
			adapter->current_op);
		return;
	}

	adapter->ch_config.state = __IAVF_TC_INVALID;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	adapter->current_op = VIRTCHNL_OP_DISABLE_CHANNELS;
	adapter->aq_required &= ~IAVF_FLAG_AQ_DISABLE_CHANNELS;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DISABLE_CHANNELS, NULL, 0);
}

/**
 * iavf_print_cloud_filter
 * @adapter: adapter structure
 * @f: cloud filter to print
 *
 * Print the cloud filter
 **/
static void iavf_print_cloud_filter(struct iavf_adapter *adapter,
				    struct virtchnl_filter *f)
{
	switch (f->flow_type) {
	case VIRTCHNL_TCP_V4_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI4 src_ip %pI4 TCP: dst_port %hu src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip[0],
			 &f->data.tcp_spec.src_ip[0],
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	case VIRTCHNL_TCP_V6_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI6 src_ip %pI6 TCP: dst_port %hu tcp_src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip,
			 &f->data.tcp_spec.src_ip,
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	case VIRTCHNL_UDP_V4_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI4 src_ip %pI4 UDP: dst_port %hu udp_src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip[0],
			 &f->data.tcp_spec.src_ip[0],
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	case VIRTCHNL_UDP_V6_FLOW:
		dev_info(&adapter->pdev->dev, "dst_mac: %pM src_mac: %pM vlan_id: %hu dst_ip: %pI6 src_ip %pI6 UDP: dst_port %hu tcp_src_port %hu\n",
			 &f->data.tcp_spec.dst_mac,
			 &f->data.tcp_spec.src_mac,
			 ntohs(f->data.tcp_spec.vlan_id),
			 &f->data.tcp_spec.dst_ip,
			 &f->data.tcp_spec.src_ip,
			 ntohs(f->data.tcp_spec.dst_port),
			 ntohs(f->data.tcp_spec.src_port));
		break;
	}
}

/**
 * iavf_add_cloud_filter
 * @adapter: adapter structure
 *
 * Request that the PF add cloud filters as specified
 * by the user via tc tool.
 **/
void iavf_add_cloud_filter(struct iavf_adapter *adapter)
{
	struct iavf_cloud_filter *cf;
	struct virtchnl_filter *f;
	bool process_fltr = false;
	int len = 0;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot add cloud filter, command %d pending\n",
			adapter->current_op);
		return;
	}

	len = sizeof(struct virtchnl_filter);
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return;

	/* Only add a single cloud filter per call to iavf_add_cloud_filter(),
	 * the aq_required IAVF_FLAG_AQ_ADD_CLOUD_FILTER bit will be set until
	 * no filters are left to add
	 */
	spin_lock_bh(&adapter->cloud_filter_list_lock);
	list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
		if (cf->add) {
			process_fltr = true;
			cf->add = false;
			cf->state = __IAVF_CF_ADD_PENDING;
			*f = cf->f;
			/* must to store channel ptr in cloud filter if action
			 * is TC_REDIRECT since it is used later
			 */
			if (f->action == VIRTCHNL_ACTION_TC_REDIRECT) {
				u32 tc = f->action_meta;

				cf->ch = &adapter->ch_config.ch_ex_info[tc];
			}
			break;
		}
	}
	spin_unlock_bh(&adapter->cloud_filter_list_lock);

	if (!process_fltr) {
		/* prevent iavf_add_cloud_filter() from being called when there
		 * are no filters to add
		 */
		adapter->aq_required &= ~IAVF_FLAG_AQ_ADD_CLOUD_FILTER;
		kfree(f);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_ADD_CLOUD_FILTER;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_ADD_CLOUD_FILTER, (u8 *)f, len);
	kfree(f);
}

/**
 * iavf_del_cloud_filter
 * @adapter: adapter structure
 *
 * Request that the PF delete cloud filters as specified
 * by the user via tc tool.
 **/
void iavf_del_cloud_filter(struct iavf_adapter *adapter)
{
	struct iavf_cloud_filter *cf;
	struct virtchnl_filter *f;
	bool process_fltr = false;
	int len = 0;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev, "Cannot remove cloud filter, command %d pending\n",
			adapter->current_op);
		return;
	}
	len = sizeof(struct virtchnl_filter);
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return;

	/* Only delete a single cloud filter per call to iavf_del_cloud_filter()
	 * the aq_required IAVF_FLAG_AQ_DEL_CLOUD_FILTER bit will be set until
	 * no filters are left to delete
	 */
	spin_lock_bh(&adapter->cloud_filter_list_lock);
	list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
		if (cf->del) {
			process_fltr = true;
			*f = cf->f;
			cf->del = false;
			cf->state = __IAVF_CF_DEL_PENDING;
			break;
		}
	}
	spin_unlock_bh(&adapter->cloud_filter_list_lock);

	if (!process_fltr) {
		/* prevent iavf_del_cloud_filter() from being called when there
		 * are no filters to delete
		 */
		adapter->aq_required &= ~IAVF_FLAG_AQ_DEL_CLOUD_FILTER;
		kfree(f);
		return;
	}
	adapter->current_op = VIRTCHNL_OP_DEL_CLOUD_FILTER;
	iavf_send_pf_msg(adapter, VIRTCHNL_OP_DEL_CLOUD_FILTER, (u8 *)f, len);
	kfree(f);
}

/**
 * iavf_request_reset
 * @adapter: adapter structure
 *
 * Request that the PF reset this VF. No response is expected.
 **/
int iavf_request_reset(struct iavf_adapter *adapter)
{
	enum iavf_status status;
	/* Don't check CURRENT_OP - this is always higher priority */
	status = iavf_send_pf_msg(adapter, VIRTCHNL_OP_RESET_VF, NULL, 0);
	adapter->current_op = VIRTCHNL_OP_UNKNOWN;
	return status;
}

/**
 * iavf_clear_chnl_ring_attr - clears  rings attributes specific to channel
 * @adapter: adapter structure
 * @ring: Pointer to ring (Tx/Rx)
 * @tx: TRUE means Tx and FALSE means Rx
 *
 * This function clears up ring attributes such as feature flag (optimization
 * enabled or not, also resets vector feature flags associated with queue)
 **/
static void iavf_clear_chnl_ring_attr(struct iavf_adapter *adapter,
				      struct iavf_ring *ring,
				      bool tx)
{
	struct iavf_q_vector *qv = ring->q_vector;

	ring->ch = NULL;
	ring->chnl_flags &= ~IAVF_RING_CHNL_PERF_ENA;
	dev_dbg(&adapter->pdev->dev,
		"%s_ring %u, ch_ena: %u, perf_ena: %u\n",
		tx ? "Tx" : "Rx", ring->queue_index, ring_ch_ena(ring),
		ring_ch_perf_ena(ring));

	if (!qv)
		return;

	qv->ch = NULL;

	/* revive the vector from ADQ state machine
	 * by triggering SW interrupt
	 */
	iavf_force_wb(&adapter->vsi, qv);
	qv->chnl_flags &= ~IAVF_VECTOR_CHNL_PERF_ENA;
	dev_dbg(&adapter->pdev->dev,
		"vector(idx: %u): ch_ena: %u, perf_ena: %u\n",
		qv->v_idx, vector_ch_ena(qv), vector_ch_perf_ena(qv));
}

/**
 * iavf_clear_ch_info - clears channel specific information and flags_
 * @adapter: adapter structure
 *
 * This function clears channel specific configurations, flags for
 * Tx, Rx queues, related vectors and triggers software interrupt
 * to revive the ADQ specific vectors, so that vector is put back in
 * interrupt state
 **/
static void iavf_clear_ch_info(struct iavf_adapter *adapter)
{
	int tc, q;

	/* to avoid running iAVF on older HW, do not want to support
	 * ADQ related performance bits, hence checking the ADQ_V2 as
	 * run-time type and prevent if ADQ_V2 is not set.
	 */
	if (!iavf_is_adq_v2_enabled(adapter))
		return;

	for (tc = 0; tc < VIRTCHNL_MAX_ADQ_V2_CHANNELS; tc++) {
		struct iavf_channel_ex *ch;
		int num_rxq;

		ch = &adapter->ch_config.ch_ex_info[tc];
		if (!ch)
			continue;

		/* unlikely but make sure to have non-zero "num_rxq" for
		 * channel otherwise skip..
		 */
		num_rxq = ch->num_rxq;
		if (!num_rxq)
			continue;

		/* proceed only when there is no active filter
		 * for given channel
		 */
		if (ch->num_fltr)
			continue;

		/* do not proceed unless we have vectors >= num_active_queues.
		 * In future, this is subject to change if interrupt to queue
		 * assignment policy changesm but for now - expect as many
		 * vectors as data_queues
		 */
		if (adapter->num_msix_vectors <= adapter->num_active_queues)
			continue;

		for (q = 0; q < num_rxq; q++) {
			struct iavf_ring *tx_ring, *rx_ring;

			tx_ring = &adapter->tx_rings[ch->base_q + q];
			rx_ring = &adapter->rx_rings[ch->base_q + q];
			if (tx_ring)
				iavf_clear_chnl_ring_attr(adapter, tx_ring,
							  true);
			if (rx_ring)
				iavf_clear_chnl_ring_attr(adapter, rx_ring,
							  false);
		}
	}
}

/**
 * iavf_set_chnl_ring_attr - sets rings attributes specific to channel
 * @adapter: adapter structure
 * @flags: adapter specific flags (various feature bits)
 * @ring: Pointer to ring (Tx/Rx)
 * @ch: Pointer to channel
 * @tx: TRUE means Tx and FALSE means Rx
 *
 * This function sets up ring attributes such as feature flag (optimization
 * enabled or not, also sets up vector feature flags associated with queue)
 **/
static void iavf_set_chnl_ring_attr(struct iavf_adapter *adapter, u32 flags,
				    struct iavf_ring *ring,
				    struct iavf_channel_ex *ch,
				    bool tx)
{
	struct iavf_q_vector *qv = ring->q_vector;

	ring->ch = ch;
	ring->chnl_flags |= IAVF_RING_CHNL_PERF_ENA;
	dev_dbg(&adapter->pdev->dev, "%s_ring %u, ch_ena: %u, perf_ena: %u\n",
		tx ? "Tx" : "Rx", ring->queue_index, ring_ch_ena(ring),
		ring_ch_perf_ena(ring));

	if (!qv)
		return;

	qv->ch = ch;
	qv->chnl_flags |= IAVF_VECTOR_CHNL_PERF_ENA;
	if (flags & IAVF_FLAG_CHNL_PKT_OPT_ENA)
		qv->chnl_flags |= IAVF_VECTOR_CHNL_PKT_OPT_ENA;
	else
		qv->chnl_flags &= ~IAVF_VECTOR_CHNL_PKT_OPT_ENA;
	dev_dbg(&adapter->pdev->dev,
		"vector(idx %u): ch_ena: %u, perf_ena: %u\n",
		qv->v_idx, vector_ch_ena(qv), vector_ch_perf_ena(qv));
}

/**
 * iavf_setup_ch_info - sets channel specific information and flags
 * @adapter: adapter structure
 * @flags: adapter specific flags (various feature bits)
 *
 * This function sets up queues (Tx and Rx) and vector specific flags
 * as appliable for ADQ. This function is invoked as soon as filters
 * were added successfully, so that queues and vectors are setup to engage
 * for optimized packets processing using ADQ state machine based logic.
 **/
void iavf_setup_ch_info(struct iavf_adapter *adapter, u32 flags)
{
	int tc;

	/* to avoid running iAVF on older HW, do not want to support
	 * ADQ related performance bits, hence checking the ADQ_V2 as
	 * run-time type and prevent if ADQ_V2 is not set.
	 */
	if (!iavf_is_adq_v2_enabled(adapter))
		return;

	for (tc = 0; tc < VIRTCHNL_MAX_ADQ_V2_CHANNELS; tc++) {
		struct iavf_channel_ex *ch;
		int num_rxq, q;

		ch = &adapter->ch_config.ch_ex_info[tc];
		if (!ch)
			continue;

		/* unlikely but make sure to have non-zero "num_rxq" for
		 * channel otherwise skip..
		 */
		num_rxq = ch->num_rxq;
		if (!num_rxq)
			continue;

		/* do not proceed unless there is at least one filter
		 * for given channel
		 */
		if (!ch->num_fltr)
			continue;

		/* do not proceed unless we have vectors >= num_active_queues.
		 * In future, this is subject to change if interrupt to queue
		 * assignment policy changesm but for now - expect as many
		 * vectors as data_queues
		 */
		if (adapter->num_msix_vectors <= adapter->num_active_queues)
			continue;

		for (q = 0; q < num_rxq; q++) {
			struct iavf_ring *tx_ring, *rx_ring;

			tx_ring = &adapter->tx_rings[ch->base_q + q];
			rx_ring = &adapter->rx_rings[ch->base_q + q];
			if (tx_ring)
				iavf_set_chnl_ring_attr(adapter, flags,
							tx_ring, ch, true);
			if (rx_ring)
				iavf_set_chnl_ring_attr(adapter, flags,
							rx_ring, ch, false);
		}
	}
}

/**
 * iavf_netdev_features_vlan_strip_set
 * @netdev: ptr to netdev being adjusted
 * @enable: enable or disable vlan strip
 *
 * Helper function to change vlan strip status in netdev->features.
 **/
static void iavf_netdev_features_vlan_strip_set(struct net_device *netdev,
						const bool enable)
{
	if (enable)
#ifdef NETIF_F_HW_VLAN_CTAG_RX
		netdev->features |= NETIF_F_HW_VLAN_CTAG_RX;
#else
		netdev->features |= NETIF_F_HW_VLAN_RX;
#endif /* NETIF_F_HW_VLAN_CTAG_RX */
	else
#ifdef NETIF_F_HW_VLAN_CTAG_RX
		netdev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
#else
		netdev->features &= ~NETIF_F_HW_VLAN_RX;
#endif /* NETIF_F_HW_VLAN_CTAG_RX */
}

/**
 * iavf_virtchnl_ptp_get_time - Respond to VIRTCHNL_OP_1588_PTP_GET_TIME
 * @adapter: private adapter structure
 * @data: the message from the PF
 * @len: length of the message from the PF
 *
 * Handle the VIRTCHNL_OP_1588_PTP_GET_TIME message from the PF. This message
 * is sent by the PF in response to the same op as a request from the VF.
 * Extract the 64bit nanoseconds time from the message and store it in
 * cached_phc_time. Then, notify any thread that is waiting for the update via
 * the wait queue.
 */
static void iavf_virtchnl_ptp_get_time(struct iavf_adapter *adapter, void *data, u16 len)
{
	struct virtchnl_phc_time *msg;

	if (len == sizeof(*msg)) {
		msg = (struct virtchnl_phc_time *)data;
	} else {
		dev_err_once(&adapter->pdev->dev, "Invalid VIRTCHNL_OP_1588_PTP_GET_TIME from PF. Got size %u, expected %lu\n",
			     len, sizeof(*msg));
		return;
	}

	adapter->ptp.cached_phc_time = msg->time;
	adapter->ptp.cached_phc_updated = jiffies;
	adapter->ptp.phc_time_ready = true;

	wake_up(&adapter->ptp.phc_time_waitqueue);
}

/**
 * iavf_virtchnl_ptp_tx_timestamp - Handle Tx timestamp events from the PF
 * @adapter: private adapter structure
 * @data: message contents from PF
 * @len: length of the message from the PF
 *
 * Handle the VIRTCHNL_OP_1588_PTP_TX_TIMESTAMP op from the PF. This is sent
 * whenever the PF has detected a transmit timestamp associated with this VF.
 *
 * First, check if there is a pending skb that needs a transmit timestamp. If
 * so, extract the time value from the message and report it to the stack.
 * Note that 40bit timestamp values must first be extended using
 * iavf_ptp_extend_40b_timestamp().
 */
static void iavf_virtchnl_ptp_tx_timestamp(struct iavf_adapter *adapter, void *data, u16 len)
{
	struct skb_shared_hwtstamps skb_tstamps = {};
	struct device *dev = &adapter->pdev->dev;
	struct virtchnl_phc_tx_tstamp *msg;
	struct sk_buff *skb;
	u64 ns;

	if (len == sizeof(*msg)) {
		msg = (struct virtchnl_phc_tx_tstamp *)data;
	} else {
		dev_err_once(dev, "Invalid VIRTCHNL_OP_1588_PTP_TX_TIMESTAMP from PF. Got size %u, expected %lu\n",
			     len, sizeof(*msg));
		return;
	}

	/* No need to process the event if timestamping isn't on */
	if (adapter->ptp.hwtstamp_config.tx_type != HWTSTAMP_TX_ON)
		return;

	/* don't attempt to timestamp if we don't have a pending skb */
	skb = adapter->ptp.tx_skb;
	if (!skb)
		return;

	/* Since we only request one outstanding timestamp at once, we assume
	 * this event must belong to the saved SKB. Clear the bit lock and the
	 * skb now prior to notifying the stack via skb_tstamp_tx().
	 */
	adapter->ptp.tx_skb = NULL;
	clear_bit_unlock(__IAVF_TX_TSTAMP_IN_PROGRESS, &adapter->crit_section);

	switch (adapter->ptp.hw_caps.tx_tstamp_format) {
	case VIRTCHNL_1588_PTP_TSTAMP_40BIT:
		if (!(msg->tstamp & IAVF_PTP_40B_TSTAMP_VALID)) {
			dev_warn(dev, "Got a VIRTCHNL_OP_1588_PTP_TX_TIMESTAMP message with an invalid timestamp\n");
			goto out_free_skb;
		}
		ns = iavf_ptp_extend_40b_timestamp(adapter->ptp.cached_phc_time, msg->tstamp);
		break;
	case VIRTCHNL_1588_PTP_TSTAMP_64BIT_NS:
		ns = msg->tstamp;
		break;
	default:
		/* This shouldn't happen since we won't enable Tx timestamps
		 * if we don't know the timestamp format.
		 */
		dev_dbg(dev, "Got a VIRTCHNL_OP_1588_PTP_TX_TIMESTAMP event, when timestamp format is unknown\n");
		goto out_free_skb;
	}

	skb_tstamps.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(skb, &skb_tstamps);

out_free_skb:
	dev_kfree_skb_any(skb);
}

/**
 * iavf_virtchnl_completion
 * @adapter: adapter structure
 * @v_opcode: opcode sent by PF
 * @v_retval: retval sent by PF
 * @msg: message sent by PF
 * @msglen: message length
 *
 * Asynchronous completion function for admin queue messages. Rather than busy
 * wait, we fire off our requests and assume that no errors will be returned.
 * This function handles the reply messages.
 **/
void iavf_virtchnl_completion(struct iavf_adapter *adapter,
			      enum virtchnl_ops v_opcode,
			      enum iavf_status v_retval,
			      u8 *msg, u16 msglen)
{
	struct net_device *netdev = adapter->netdev;

	if (v_opcode == VIRTCHNL_OP_EVENT) {
		struct virtchnl_pf_event *vpe =
			(struct virtchnl_pf_event *)msg;
#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
		bool link_up = iavf_get_vpe_link_status(adapter, vpe);
#else
		bool link_up = vpe->event_data.link_event.link_status;
#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */

		switch (vpe->event) {
		case VIRTCHNL_EVENT_LINK_CHANGE:
#ifdef VIRTCHNL_VF_CAP_ADV_LINK_SPEED
			iavf_set_adapter_link_speed_from_vpe(adapter, vpe);
#else
			adapter->link_speed =
				vpe->event_data.link_event.link_speed;
#endif /* VIRTCHNL_VF_CAP_ADV_LINK_SPEED */

			/* we've already got the right link status, bail */
			if (adapter->link_up == link_up)
				break;

			if (link_up) {
				/* If we get link up message and start queues
				 * before our queues are configured it will
				 * trigger a TX hang. In that case, just ignore
				 * the link status message,we'll get another one
				 * after we enable queues and actually prepared
				 * to send traffic.
				 */
				if (adapter->state != __IAVF_RUNNING)
					break;

				/* For ADQ enabled VF, we reconfigure VSIs and
				 * re-allocate queues. Hence wait till all
				 * queues are enabled.
				 */
				if (adapter->flags &
				    IAVF_FLAG_QUEUES_DISABLED)
					break;
			}

			adapter->link_up = link_up;
			if (link_up) {
				if  (adapter->flags &
				     IAVF_FLAG_QUEUES_ENABLED) {
					netif_tx_start_all_queues(netdev);
					netif_carrier_on(netdev);
				}
				if (!ether_addr_equal(netdev->dev_addr,
						      adapter->hw.mac.addr))
					iavf_replace_primary_mac
						(adapter, netdev->dev_addr);
			} else {
				netif_tx_stop_all_queues(netdev);
				netif_carrier_off(netdev);
			}
			iavf_print_link_message(adapter);
			break;
		case VIRTCHNL_EVENT_RESET_IMPENDING:
			dev_info(&adapter->pdev->dev, "Reset indication received from the PF\n");
			adapter->flags |= IAVF_FLAG_RESET_PENDING;
			iavf_schedule_reset(adapter);
			break;
		default:
			dev_err(&adapter->pdev->dev, "Unknown event %d from PF\n",
				vpe->event);
			break;
		}
		return;
	}

	/* In earlier versions of ADQ implementation, VF reset was initiated by
	 * PF in response to enable ADQ request from VF. However for performance
	 * of ADQ we need the response back and based on that additional configs
	 * will be done. So don't let the PF reset the VF instead let the VF
	 * reset itself.
	 */
	if (ADQ_V2_ALLOWED(adapter) && !v_retval &&
	    (v_opcode == VIRTCHNL_OP_ENABLE_CHANNELS ||
	     v_opcode == VIRTCHNL_OP_DISABLE_CHANNELS)) {
		adapter->flags |= IAVF_FLAG_REINIT_CHNL_NEEDED;
		dev_info(&adapter->pdev->dev,
			 "Scheduling reset due to %s retval %d\n",
			 v_opcode == VIRTCHNL_OP_ENABLE_CHANNELS ?
			 "VIRTCHNL_OP_ENABLE_CHANNELS" :
			 "VIRTCHNL_OP_DISABLE_CHANNELS", v_retval);
		/* schedule reset always if processing ENABLE/DISABLE_CHANNEL
		 * ops so that as part of reset handling, appropriate steps are
		 * taken such as num_tc, per TC queue_map, etc...
		 */
		iavf_schedule_reset(adapter);
	}

	if (v_retval) {
		switch (v_opcode) {
		case VIRTCHNL_OP_ADD_VLAN:
			dev_err(&adapter->pdev->dev, "Failed to add VLAN filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_ADD_ETH_ADDR:
			dev_err(&adapter->pdev->dev, "Failed to add MAC filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			iavf_mac_add_reject(adapter);
			/* restore administratively set mac address */
			ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);
			break;
		case VIRTCHNL_OP_DEL_VLAN:
			dev_err(&adapter->pdev->dev, "Failed to delete VLAN filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_DEL_ETH_ADDR:
			dev_err(&adapter->pdev->dev, "Failed to delete MAC filter, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			break;
		case VIRTCHNL_OP_ENABLE_CHANNELS:
			dev_err(&adapter->pdev->dev, "Failed to configure queue channels, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
			adapter->ch_config.state = __IAVF_TC_INVALID;
			netdev_reset_tc(netdev);
			netif_tx_start_all_queues(netdev);
			break;
		case VIRTCHNL_OP_DISABLE_CHANNELS:
			dev_err(&adapter->pdev->dev, "Failed to disable queue channels, error %s\n",
				iavf_stat_str(&adapter->hw, v_retval));
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
			adapter->ch_config.state = __IAVF_TC_RUNNING;
			netif_tx_start_all_queues(netdev);
			break;
		case VIRTCHNL_OP_ADD_CLOUD_FILTER: {
			struct iavf_cloud_filter *cf, *cftmp;

			spin_lock_bh(&adapter->cloud_filter_list_lock);
			list_for_each_entry_safe(cf, cftmp,
						 &adapter->cloud_filter_list,
						 list) {
				if (cf->state == __IAVF_CF_ADD_PENDING) {
					cf->state = __IAVF_CF_INVALID;
					dev_info(&adapter->pdev->dev, "Failed to add cloud filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_cloud_filter(adapter,
								&cf->f);
					if (msglen)
						dev_err(&adapter->pdev->dev,
							"%s\n", msg);
					list_del(&cf->list);
					kfree(cf);
					adapter->num_cloud_filters--;
				}
			}
			spin_unlock_bh(&adapter->cloud_filter_list_lock);
			}
			break;
		case VIRTCHNL_OP_DEL_CLOUD_FILTER: {
			struct iavf_cloud_filter *cf;

			spin_lock_bh(&adapter->cloud_filter_list_lock);
			list_for_each_entry(cf, &adapter->cloud_filter_list,
					    list) {
				if (cf->state == __IAVF_CF_DEL_PENDING) {
					cf->state = __IAVF_CF_ACTIVE;
					dev_info(&adapter->pdev->dev, "Failed to del cloud filter, error %s\n",
						 iavf_stat_str(&adapter->hw,
							       v_retval));
					iavf_print_cloud_filter(adapter,
								&cf->f);
				}
			}
			spin_unlock_bh(&adapter->cloud_filter_list_lock);
			}
			break;
		case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
			dev_warn(&adapter->pdev->dev,
				 "Changing VLAN Stripping is not allowed when Port VLAN is configured\n");
			/*
			 * Vlan stripping could not be enabled by ethtool.
			 * Disable it in netdev->features.
			 */
			iavf_netdev_features_vlan_strip_set(netdev, false);
			break;
		case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
			dev_warn(&adapter->pdev->dev,
				 "Changing VLAN Stripping is not allowed when Port VLAN is configured\n");
			/*
			 * Vlan stripping could not be disabled by ethtool.
			 * Enable it in netdev->features.
			 */
			iavf_netdev_features_vlan_strip_set(netdev, true);
			break;
		default:
			dev_err(&adapter->pdev->dev, "PF returned error %d (%s) to our request %d\n",
				v_retval, iavf_stat_str(&adapter->hw, v_retval),
				v_opcode);

			/* Assume that the ADQ configuration caused one of the
			 * v_opcodes in this if statement to fail.  Set the
			 * flag so the reset path can return to the pre-ADQ
			 * configuration and traffic can resume
			 */
			if (iavf_is_adq_enabled(adapter) &&
			    (v_opcode == VIRTCHNL_OP_ENABLE_QUEUES ||
			     v_opcode == VIRTCHNL_OP_CONFIG_IRQ_MAP ||
			     v_opcode == VIRTCHNL_OP_CONFIG_VSI_QUEUES)) {
				dev_err(&adapter->pdev->dev,
					"ADQ is enabled and opcode %d failed (%d)\n",
					v_opcode, v_retval);
				adapter->ch_config.state = __IAVF_TC_INVALID;
				adapter->num_tc = 0;
				netdev_reset_tc(netdev);
				adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
				iavf_schedule_reset(adapter);
				adapter->current_op = VIRTCHNL_OP_UNKNOWN;
				return;
			}
		}
	}
	switch (v_opcode) {
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		if (!v_retval)
			iavf_mac_add_ok(adapter);
		if (!ether_addr_equal(netdev->dev_addr, adapter->hw.mac.addr))
			ether_addr_copy(adapter->hw.mac.addr, netdev->dev_addr);
		break;
	case VIRTCHNL_OP_GET_STATS: {
		struct iavf_eth_stats *stats =
			(struct iavf_eth_stats *)msg;
		adapter->net_stats.rx_packets = stats->rx_unicast +
						 stats->rx_multicast +
						 stats->rx_broadcast;
		adapter->net_stats.tx_packets = stats->tx_unicast +
						 stats->tx_multicast +
						 stats->tx_broadcast;
		adapter->net_stats.rx_bytes = stats->rx_bytes;
		adapter->net_stats.tx_bytes = stats->tx_bytes;
		adapter->net_stats.tx_errors = stats->tx_errors;
		adapter->net_stats.rx_dropped = stats->rx_discards;
		adapter->net_stats.tx_dropped = stats->tx_discards;
		adapter->current_stats = *stats;
		}
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES: {
		u16 len = sizeof(struct virtchnl_vf_resource) +
			  IAVF_MAX_VF_VSI *
			  sizeof(struct virtchnl_vsi_resource);

		memcpy(adapter->vf_res, msg, min(msglen, len));
		iavf_validate_num_queues(adapter);
		iavf_vf_parse_hw_config(&adapter->hw, adapter->vf_res);
		if (is_zero_ether_addr(adapter->hw.mac.addr)) {
			/* restore current mac address */
			ether_addr_copy(adapter->hw.mac.addr, netdev->dev_addr);
		} else {
			/* refresh current mac address if changed */
			ether_addr_copy(netdev->perm_addr,
					adapter->hw.mac.addr);
		}

		iavf_parse_vf_resource_msg(adapter);

		/* negotiated VIRTCHNL_VF_OFFLOAD_VLAN_V2, so wait for the
		 * response to VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS to finish
		 * configuration
		 */
		if (VLAN_V2_ALLOWED(adapter))
			break;
		/* fall-through and finish config if VIRTCHNL_VF_OFFLOAD_VLAN_V2
		 * wasn't successfully negotiated with the PF
		 */
		}
		/* fall-through */
	case VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS: {
		struct iavf_mac_filter *f;
		bool was_mac_changed;

		if (v_opcode == VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS)
			memcpy(&adapter->vlan_v2_caps, msg,
			       min_t(u16, msglen,
				     sizeof(adapter->vlan_v2_caps)));

		iavf_process_config(adapter);

		/* Clear 'critical task' bit before acquiring rtnl_lock
		 * as other process holding rtnl_lock could be waiting
		 * for the same bit resulting in deadlock
		 */
		clear_bit(__IAVF_IN_CRITICAL_TASK, &adapter->crit_section);
		/* VLAN capabilities can change during VFR, so make sure to
		 * update the netdev features with the new capabilities
		 */
		rtnl_lock();
		netdev_update_features(netdev);
		rtnl_unlock();
		/* Set 'critical task' bit again */
		while (test_and_set_bit(__IAVF_IN_CRITICAL_TASK,
					&adapter->crit_section))
			usleep_range(500, 1000);

		iavf_set_queue_vlan_tag_loc(adapter);

		was_mac_changed = !ether_addr_equal(netdev->dev_addr,
						    adapter->hw.mac.addr);

		spin_lock_bh(&adapter->mac_vlan_list_lock);

		/* re-add all MAC filters */
		list_for_each_entry(f, &adapter->mac_filter_list, list) {
			if (was_mac_changed &&
			    ether_addr_equal(netdev->dev_addr, f->macaddr))
				ether_addr_copy(f->macaddr,
						adapter->hw.mac.addr);

			f->is_new_mac = true;
			f->add = true;
			f->remove = false;
		}

		/* re-add all VLAN filters */
		if (VLAN_FILTERING_ALLOWED(adapter)) {
			struct iavf_vlan_filter *vlf;

			list_for_each_entry(vlf, &adapter->vlan_filter_list,
					    list)
				vlf->add = true;
		}

		spin_unlock_bh(&adapter->mac_vlan_list_lock);

		/* check if TCs are running and re-add all cloud filters
		 * Set ADD_CLOUD_FILTER only if list is not empty so that
		 * re-add of filters can happen correctly
		 */
		if (iavf_is_adq_enabled(adapter) ||
		    iavf_is_adq_v2_enabled(adapter)) {
			struct iavf_cloud_filter *cf;

			spin_lock_bh(&adapter->cloud_filter_list_lock);
			if (!list_empty(&adapter->cloud_filter_list)) {
				list_for_each_entry(cf,
						    &adapter->cloud_filter_list,
						    list) {
					cf->add = true;
				}
				adapter->aq_required |=
						IAVF_FLAG_AQ_ADD_CLOUD_FILTER;
			}
			spin_unlock_bh(&adapter->cloud_filter_list_lock);
		}

		ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);

		adapter->aq_required |= IAVF_FLAG_AQ_ADD_MAC_FILTER;
		adapter->aq_required |= IAVF_FLAG_AQ_ADD_VLAN_FILTER;
		}
		break;
	case VIRTCHNL_OP_GET_SUPPORTED_RXDIDS:
		memcpy(&adapter->supported_rxdids, msg,
		       min_t(u16, msglen,
			     sizeof(adapter->supported_rxdids)));
		break;
	case VIRTCHNL_OP_1588_PTP_GET_CAPS:
		memcpy(&adapter->ptp.hw_caps, msg,
		       min_t(u16, msglen, sizeof(adapter->ptp.hw_caps)));
		/* process any state change needed due to new capabilities */
		iavf_ptp_process_caps(adapter);
		break;
	case VIRTCHNL_OP_1588_PTP_GET_TIME:
		iavf_virtchnl_ptp_get_time(adapter, msg, msglen);
		break;
	case VIRTCHNL_OP_1588_PTP_TX_TIMESTAMP:
		iavf_virtchnl_ptp_tx_timestamp(adapter, msg, msglen);
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		/* enable transmits */
		if (adapter->state == __IAVF_RUNNING) {
			iavf_irq_enable(adapter, true);

			/* If queues not enabled when handling link event,
			 * then set carrier on now
			 */
			if (adapter->link_up && !netif_carrier_ok(netdev)) {
				netif_tx_start_all_queues(netdev);
				netif_carrier_on(netdev);
			}
		}
		adapter->flags |= IAVF_FLAG_QUEUES_ENABLED;
		adapter->flags &= ~IAVF_FLAG_QUEUES_DISABLED;
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		iavf_free_all_tx_resources(adapter);
		iavf_free_all_rx_resources(adapter);
		if (adapter->state == __IAVF_DOWN_PENDING) {
			iavf_change_state(adapter, __IAVF_DOWN);
			wake_up(&adapter->down_waitqueue);
		}
		adapter->flags &= ~IAVF_FLAG_QUEUES_ENABLED;
		break;
	case VIRTCHNL_OP_VERSION:
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		/* Don't display an error if we get these out of sequence.
		 * If the firmware needed to get kicked, we'll get these and
		 * it's no problem.
		 */
		if (v_opcode != adapter->current_op)
			return;
		break;
	case VIRTCHNL_OP_GET_RSS_HENA_CAPS: {
		struct virtchnl_rss_hena *vrh = (struct virtchnl_rss_hena *)msg;
		if (msglen == sizeof(*vrh))
			adapter->hena = vrh->hena;
		else
			dev_warn(&adapter->pdev->dev,
				 "Invalid message %d from PF\n", v_opcode);
		}
		break;
	case VIRTCHNL_OP_REQUEST_QUEUES: {
		struct virtchnl_vf_res_request *vfres =
			(struct virtchnl_vf_res_request *)msg;
		if (vfres->num_queue_pairs != adapter->num_req_queues) {
			dev_info(&adapter->pdev->dev,
				 "Requested %d queues, PF can support %d\n",
				 adapter->num_req_queues,
				 vfres->num_queue_pairs);
			adapter->num_req_queues = 0;
			adapter->flags &= ~IAVF_FLAG_REINIT_ITR_NEEDED;
		}
		}
		break;
	case VIRTCHNL_OP_ADD_CLOUD_FILTER: {
		struct iavf_cloud_filter *cf;

		spin_lock_bh(&adapter->cloud_filter_list_lock);
		list_for_each_entry(cf, &adapter->cloud_filter_list, list) {
			if (cf->state == __IAVF_CF_ADD_PENDING) {
				cf->state = __IAVF_CF_ACTIVE;
				if (cf->ch)
					cf->ch->num_fltr++;
			}
		}
		spin_unlock_bh(&adapter->cloud_filter_list_lock);
		if (!v_retval)
			dev_info(&adapter->pdev->dev,
				 "Cloud filters are added\n");
			/* if not done, set channel specific attribute
			 * such as "is it ADQ enabled", "queues are ADD ena",
			 * "vectors are ADQ ena" or not
			 */
			iavf_setup_ch_info(adapter, adapter->flags);
		}
		break;
	case VIRTCHNL_OP_DEL_CLOUD_FILTER: {
		struct iavf_cloud_filter *cf, *cftmp;

		spin_lock_bh(&adapter->cloud_filter_list_lock);
		list_for_each_entry_safe(cf, cftmp, &adapter->cloud_filter_list,
					 list) {
			if (cf->state == __IAVF_CF_DEL_PENDING) {
				cf->state = __IAVF_CF_INVALID;
				list_del(&cf->list);
				if (cf->ch)
					cf->ch->num_fltr--;
				kfree(cf);
				adapter->num_cloud_filters--;
			}
		}
		spin_unlock_bh(&adapter->cloud_filter_list_lock);
		if (!v_retval)
			dev_info(&adapter->pdev->dev,
				 "Cloud filters are deleted\n");
			/* if active ADQ filters for channels reached zero,
			 * put the rings, vectors back in non-ADQ state
			 */
			iavf_clear_ch_info(adapter);
		}
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
		/*
		 * Got information that PF enabled vlan strip on this VF.
		 * Update netdev->features if needed to be in sync with ethtool.
		 */
		if (!v_retval)
			iavf_netdev_features_vlan_strip_set(netdev, true);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
		/*
		 * Got information that PF disabled vlan strip on this VF.
		 * Update netdev->features if needed to be in sync with ethtool.
		 */
		if (!v_retval)
			iavf_netdev_features_vlan_strip_set(netdev, false);
		break;
	default:
		if (adapter->current_op && (v_opcode != adapter->current_op))
			dev_dbg(&adapter->pdev->dev, "Expected response %d from PF, received %d\n",
				adapter->current_op, v_opcode);
		break;
	} /* switch v_opcode */
	adapter->current_op = VIRTCHNL_OP_UNKNOWN;
}
