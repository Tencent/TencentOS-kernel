/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2018 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/
/* See Fibre Channel protocol T11 FC-SP-2 for details */

/* AUTH_ELS Message header */
struct lpfc_auth_hdr {
	uint8_t			auth_els_code;	/* 0x90 */
	uint8_t			auth_els_flags;
	uint8_t			auth_msg_code;
	uint8_t			protocol_ver;	/* version = 1 */
	uint32_t		msg_len;	/* msg payload len */
	uint32_t		tran_id;	/* transaction id */
};

/* protocol_ver defines */
#define	AUTH_PROTOCOL_VER_1	0x1

/* auth_msg_code defines */
#define	AUTH_REJECT		0x0A
#define	AUTH_NEGOTIATE		0x0B
#define	AUTH_DONE		0x0C
#define	DHCHAP_CHALLENGE	0x10
#define	DHCHAP_REPLY		0x11
#define	DHCHAP_SUCCESS		0x12

/* Auth Parameter Header */
struct lpfc_auth_param_hdr {
	uint16_t	tag;
	uint16_t	wcnt;
};

struct lpfc_dhchap_param_cmn {
	uint32_t		param_len;
	uint32_t		protocol_id;	/* DH-CHAP = 1 */
						/* DH-CHAP specific */
						/* variable length data */
};

/* AUTH_ELS Negotiate payload */
struct lpfc_auth_negotiate_cmn {
	uint16_t	name_tag;		/* tag = 1 */
	uint16_t	name_len;		/* len = 8 */
	uint32_t	port_name[2];		/* WWPN */
	uint32_t	num_protocol;		/* only 1 protocol for now */
						/* Protocol specific */
						/* variable length data */
};

/* name_tag defines */
#define	AUTH_NAME_TAG		1

/* protocol_id defines */
#define	PROTOCOL_DHCHAP		1
#define	PROTOCOL_FCAP		2
#define	PROTOCOL_FCPAP		3
#define	PROTOCOL_KERBEROS	4

/* DHCHAP param tag defines */
#define	DHCHAP_TAG_HASHLIST	1
#define	DHCHAP_TAG_GRP_IDLIST	2

/* DHCHAP Group ID defines */
#define DH_GROUP_NULL   0x00
#define DH_GROUP_1024   0x01
#define DH_GROUP_1280   0x02
#define DH_GROUP_1536   0x03
#define DH_GROUP_2048   0x04

/* hash function defines */
#define HASH_MD5        5
#define HASH_SHA1       6

#define	MD5_CHAL_LEN	16
#define	SHA1_CHAL_LEN	20

#define AUTH_DIRECTION_NONE	0
#define AUTH_DIRECTION_REMOTE	1
#define AUTH_DIRECTION_LOCAL	2
#define AUTH_DIRECTION_BIDI	(AUTH_DIRECTION_LOCAL | AUTH_DIRECTION_REMOTE)

#define AUTH_FABRIC_WWN         0xFFFFFFFFFFFFFFFFLL

enum auth_state {
	LPFC_AUTH_UNKNOWN		=  0,
	LPFC_AUTH_SUCCESS		=  1,
	LPFC_AUTH_FAIL			=  2,
};

enum auth_msg_state {
	LPFC_AUTH_NONE			=  0,
	LPFC_AUTH_REJECT		=  1,	/* Sent a Reject */
	LPFC_AUTH_NEGOTIATE		=  2,	/* Auth Negotiate */
	LPFC_DHCHAP_CHALLENGE		=  3,	/* Challenge */
	LPFC_DHCHAP_REPLY		=  4,	/* Reply */
	LPFC_DHCHAP_SUCCESS_REPLY	=  5,	/* Success with Reply */
	LPFC_DHCHAP_SUCCESS		=  6,	/* Success */
	LPFC_AUTH_DONE			=  7,
};

#define	LPFC_AUTH_STATE_UNKNOWN		0
#define	LPFC_AUTH_STATE_SUCCESS		1
#define	LPFC_AUTH_STATE_FAIL		2

#define	DHC_STATE_AUTH_NEGO	1
#define	DHC_STATE_AUTH_CHAL	2
#define	DHC_STATE_AUTH_REPLY	3
#define	DHC_STATE_AUTH_SUCCESS	4
#define	DHC_STATE_AUTH_FAIL	5

/* AUTH_ELS Challenge payload */
struct lpfc_auth_challenge {
	uint16_t		name_tag;	/* tag = 1 */
	uint16_t		name_len;	/* len = 8 */
	uint32_t		port_name[2];	/* WWPN */
	uint32_t		hash_id;	/* Hash function id */
	uint32_t		dh_grp_id;	/* DH_Group id */
	uint32_t		chal_len;	/* Challenge length */
						/* Variable length data */
};

/* AUTH Reject reason code defines */
#define	AUTHRJT_FAILURE		1
#define	AUTHRJT_LOGICAL_ERR	2

/* AUTH Reject reason code explanation defines */
#define	AUTHEXPL_MECH_UNUSABLE		1
#define	AUTHEXPL_DHGRP_UNUSABLE		2
#define	AUTHEXPL_HASH_UNUSABLE		3
#define	AUTHEXPL_AUTH_STARTED		4
#define	AUTHEXPL_AUTH_FAILED		5
#define	AUTHEXPL_BAD_PAYLOAD		6
#define	AUTHEXPL_BAD_PROTOCOL		7
#define	AUTHEXPL_RESTART_AUTH		8

/* AUTH_ELS Reject payload */
struct lpfc_auth_reject {
	uint8_t		rsn_code;
	uint8_t		rsn_expl;
	uint8_t		rsvd[2];
};

struct dh_group {
	uint32_t groupid;
	uint32_t length;
	uint8_t value[256];
};

#define LPFC_AUTH_PSSWD_MAX_LEN 128
struct lpfc_auth_pwd_entry {
	uint8_t			local_pw_length;
	uint8_t			local_pw_mode;
	uint8_t			remote_pw_length;
	uint8_t			remote_pw_mode;
	uint8_t			local_pw[LPFC_AUTH_PSSWD_MAX_LEN];
	uint8_t			remote_pw[LPFC_AUTH_PSSWD_MAX_LEN];
};

struct lpfc_auth_cfg_hdr {
	uint32_t		signature;
	uint8_t			version;
	uint8_t			size;
	uint16_t		entry_cnt;
};

struct lpfc_auth_cfg_entry {
	uint8_t			local_wwn[8];
	uint8_t			remote_wwn[8];
	uint16_t		auth_tmo;
#define LPFC_AUTH_MODE_DISABLE 1
#define LPFC_AUTH_MODE_ACTIVE  2
#define LPFC_AUTH_MODE_PASSIVE 3
	uint8_t			auth_mode;
#define LPFC_AUTH_BIDIR_DISABLE 0
#define LPFC_AUTH_BIDIR_ENABLE  1
	struct {
		uint8_t bi_direction	: 1;
		uint8_t rsvd		: 6;
		uint8_t valid		: 1;
	} auth_flags;
	uint8_t			auth_priority[4];
#define LPFC_HASH_MD5		1
#define LPFC_HASH_SHA1		2
	uint8_t			hash_priority[4];
#define LPFC_DH_GROUP_NULL	1
#define LPFC_DH_GROUP_1024	2
#define LPFC_DH_GROUP_1280	3
#define LPFC_DH_GROUP_1536	4
#define LPFC_DH_GROUP_2048	5
	uint8_t			dh_grp_priority[8];
	uint32_t		reauth_interval;
	struct lpfc_auth_pwd_entry	pwd;
};

#define LPFC_AUTH_OBJECT_MAX_SIZE  0x8000
#define MAX_AUTH_CONFIG_ENTRIES	16
#define AUTH_CONFIG_ENTRIES_PER_PAGE ((SLI4_PAGE_SIZE - \
	sizeof(struct lpfc_auth_cfg_hdr)) / sizeof(struct lpfc_auth_cfg_entry))

struct lpfc_auth_cfg {
	struct lpfc_auth_cfg_hdr	hdr;
	struct lpfc_auth_cfg_entry	entry[MAX_AUTH_CONFIG_ENTRIES];
};

