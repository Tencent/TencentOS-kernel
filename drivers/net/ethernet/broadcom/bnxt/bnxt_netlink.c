/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#include <linux/netdevice.h>
#include <linux/pci.h>
#include "bnxt_compat.h"
#include "bnxt_hsi.h"
#include "bnxt_netlink.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"

/* attribute policy */
static struct nla_policy bnxt_netlink_policy[BNXT_NUM_ATTRS] = {
	[BNXT_ATTR_PID] = { .type = NLA_U32 },
	[BNXT_ATTR_IF_INDEX] = { .type = NLA_U32 },
	[BNXT_ATTR_REQUEST] = { .type = NLA_BINARY },
	[BNXT_ATTR_RESPONSE] = { .type = NLA_BINARY },
};

static struct genl_family bnxt_netlink_family;

static int bnxt_parse_attrs(struct nlattr **a, struct bnxt **bp,
			    struct net_device **dev)
{
	pid_t pid;
	struct net *ns = NULL;
	const char *drivername;

	if (!a[BNXT_ATTR_PID]) {
		netdev_err(*dev, "No process ID specified\n");
		goto err_inval;
	}
	pid = nla_get_u32(a[BNXT_ATTR_PID]);
	ns = get_net_ns_by_pid(pid);
	if (IS_ERR(ns)) {
		netdev_err(*dev, "Invalid net namespace for pid %d (err: %ld)\n",
			pid, PTR_ERR(ns));
		goto err_inval;
	}

	if (!a[BNXT_ATTR_IF_INDEX]) {
		netdev_err(*dev, "No interface index specified\n");
		goto err_inval;
	}
	*dev = dev_get_by_index(ns, nla_get_u32(a[BNXT_ATTR_IF_INDEX]));

	put_net(ns);
	ns = NULL;
	if (!*dev) {
		netdev_err(*dev, "Invalid network interface index %d (err: %ld)\n",
		       nla_get_u32(a[BNXT_ATTR_IF_INDEX]), PTR_ERR(ns));
		goto err_inval;
	}
	if (!(*dev)->dev.parent || !(*dev)->dev.parent->driver ||
	    !(*dev)->dev.parent->driver->name) {
		netdev_err(*dev, "Unable to get driver name for device %s\n",
		       (*dev)->name);
		goto err_inval;
	}
	drivername = (*dev)->dev.parent->driver->name;
	if (strcmp(drivername, DRV_MODULE_NAME)) {
		netdev_err(*dev, "Device %s (%s) is not a %s device!\n",
		       (*dev)->name, drivername, DRV_MODULE_NAME);
		goto err_inval;
	}
	*bp = netdev_priv(*dev);
	if (!*bp) {
		netdev_warn((*bp)->dev, "No private data\n");
		goto err_inval;
	}

	return 0;

err_inval:
	if (ns && !IS_ERR(ns))
		put_net(ns);
	return -EINVAL;
}

/* handler */
static int bnxt_netlink_hwrm(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct net_device *dev = NULL;
	struct sk_buff *reply = NULL;
	struct input *req, *nl_req;
	struct bnxt *bp = NULL;
	struct output *resp;
	int len, rc;
	void *hdr;

	rc = bnxt_parse_attrs(a, &bp, &dev);
	if (rc)
		goto err_rc;

	if (!bp) {
		rc = -EINVAL;
		goto err_rc;
	}

	if (!bp->hwrm_dma_pool) {
		netdev_warn(bp->dev, "HWRM interface not currently available on %s\n",
		       dev->name);
		rc = -EINVAL;
		goto err_rc;
	}

	if (!a[BNXT_ATTR_REQUEST]) {
		netdev_warn(bp->dev, "No request specified\n");
		rc = -EINVAL;
		goto err_rc;
	}
	len = nla_len(a[BNXT_ATTR_REQUEST]);
	nl_req = nla_data(a[BNXT_ATTR_REQUEST]);

	reply = genlmsg_new(PAGE_SIZE, GFP_KERNEL);
	if (!reply) {
		netdev_warn(bp->dev, "Error: genlmsg_new failed\n");
		rc = -ENOMEM;
		goto err_rc;
	}

	rc = hwrm_req_init(bp, req, nl_req->req_type);
	if (rc)
		goto err_rc;

	rc = hwrm_req_replace(bp, req, nl_req, len);
	if (rc)
		goto err_rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (rc) {
		/*
		 * Indicate success for the hwrm transport, while letting
		 * the hwrm error be passed back to the netlink caller in
		 * the response message.  Caller is responsible for handling
		 * any errors.
		 *
		 * no kernel warnings are logged in this case.
		 */
		rc = 0;
	}
	hdr = genlmsg_put_reply(reply, info, &bnxt_netlink_family, 0,
				BNXT_CMD_HWRM);
	if (nla_put(reply, BNXT_ATTR_RESPONSE, resp->resp_len, resp)) {
		netdev_warn(bp->dev, "No space for response attribte\n");
		hwrm_req_drop(bp, req);
		rc = -ENOMEM;
		goto err_rc;
	}
	genlmsg_end(reply, hdr);
	hwrm_req_drop(bp, req);

	dev_put(dev);
	dev = NULL;

	return genlmsg_reply(reply, info);

err_rc:
	if (reply && !IS_ERR(reply))
		kfree_skb(reply);
	if (dev && !IS_ERR(dev))
		dev_put(dev);

	if (bp)
		netdev_warn(bp->dev, "returning with error code %d\n", rc);

	return rc;
}

/* handlers */
#if defined(HAVE_GENL_REG_OPS_GRPS) || !defined(HAVE_GENL_REG_FAMILY_WITH_OPS)
static const struct genl_ops bnxt_netlink_ops[] = {
#else
static struct genl_ops bnxt_netlink_ops[] = {
#endif
	{
		.cmd = BNXT_CMD_HWRM,
		.flags = GENL_ADMIN_PERM, /* Req's CAP_NET_ADMIN privilege */
#ifndef HAVE_GENL_POLICY
		.policy = bnxt_netlink_policy,
#endif
		.doit = bnxt_netlink_hwrm,
		.dumpit = NULL,
	},
};

/* family definition */
static struct genl_family bnxt_netlink_family = {
#ifdef HAVE_GENL_ID_GENERATE
	.id = GENL_ID_GENERATE,
#endif
	.hdrsize = 0,
	.name = BNXT_NL_NAME,
	.version = BNXT_NL_VER,
	.maxattr = BNXT_NUM_ATTRS,
#ifdef HAVE_GENL_POLICY
	.policy = bnxt_netlink_policy,
#endif
#ifndef HAVE_GENL_REG_FAMILY_WITH_OPS
	.ops = bnxt_netlink_ops,
	.n_ops = ARRAY_SIZE(bnxt_netlink_ops)
#endif
};

int bnxt_nl_register(void)
{
#ifndef HAVE_GENL_REG_FAMILY_WITH_OPS
	return genl_register_family(&bnxt_netlink_family);
#elif defined(HAVE_GENL_REG_OPS_GRPS)
	return genl_register_family_with_ops(&bnxt_netlink_family,
					     bnxt_netlink_ops);
#else
	return genl_register_family_with_ops(&bnxt_netlink_family,
					     bnxt_netlink_ops,
					     ARRAY_SIZE(bnxt_netlink_ops));
#endif

	return 0;
}

int bnxt_nl_unregister(void)
{
	return genl_unregister_family(&bnxt_netlink_family);
}
