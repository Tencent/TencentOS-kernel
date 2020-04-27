/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#ifndef __BNXT_NETLINK_H__
#define __BNXT_NETLINK_H__

#include <net/genetlink.h>
#include <net/netlink.h>

/* commands */
enum {
	BNXT_CMD_UNSPEC,
	BNXT_CMD_HWRM,
	BNXT_NUM_CMDS
};
#define BNXT_CMD_MAX (BNXT_NUM_CMDS - 1)

/* attributes */
enum {
	BNXT_ATTR_UNSPEC,
	BNXT_ATTR_PID,
	BNXT_ATTR_IF_INDEX,
	BNXT_ATTR_REQUEST,
	BNXT_ATTR_RESPONSE,
	BNXT_NUM_ATTRS
};
#define BNXT_ATTR_MAX (BNXT_NUM_ATTRS - 1)

#define BNXT_NL_NAME "bnxt_netlink"
#define BNXT_NL_VER  1

int bnxt_nl_register(void);
int bnxt_nl_unregister(void);

#endif
