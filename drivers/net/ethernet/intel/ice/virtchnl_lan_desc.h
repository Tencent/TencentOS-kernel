/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

/*
 * Copyright (C) 2019 Intel Corporation
 *
 * For licensing information, see the file 'LICENSE' in the root folder
 */
#ifndef _VIRTCHNL_LAN_DESC_H_
#define _VIRTCHNL_LAN_DESC_H_

/* Rx */
/* For virtchnl_splitq_base_rx_flex desc members */
#define VIRTCHNL_RXD_FLEX_RXDID_S		0
#define VIRTCHNL_RXD_FLEX_RXDID_M		\
	ICE_M(0xFUL, VIRTCHNL_RXD_FLEX_RXDID_S)
#define VIRTCHNL_RXD_FLEX_PTYPE_S		0
#define VIRTCHNL_RXD_FLEX_PTYPE_M		\
	ICE_M(0x3FFUL, VIRTCHNL_RXD_FLEX_PTYPE_S)
#define VIRTCHNL_RXD_FLEX_UMBCAST_S		10
#define VIRTCHNL_RXD_FLEX_UMBCAST_M		\
	ICE_M(0x3UL, VIRTCHNL_RXD_FLEX_UMBCAST_S)
#define VIRTCHNL_RXD_FLEX_FF0_S		12
#define VIRTCHNL_RXD_FLEX_FF0_M		ICE_M(0xFUL, VIRTCHNL_RXD_FLEX_FF0_S)
#define VIRTCHNL_RXD_FLEX_LEN_PBUF_S	0
#define VIRTCHNL_RXD_FLEX_LEN_PBUF_M	\
	ICE_M(0x3FFFUL, VIRTCHNL_RXD_FLEX_LEN_PBUF_S)
#define VIRTCHNL_RXD_FLEX_GEN_S		14
#define VIRTCHNL_RXD_FLEX_GEN_M		BIT_ULL(VIRTCHNL_RXD_FLEX_GEN_S)
#define VIRTCHNL_RXD_FLEX_BUFQ_ID_S		15
#define VIRTCHNL_RXD_FLEX_BUFQ_ID_M		\
	BIT_ULL(VIRTCHNL_RXD_FLEX_BUFQ_ID_S)
#define VIRTCHNL_RXD_FLEX_LEN_HDR_S		0
#define VIRTCHNL_RXD_FLEX_LEN_HDR_M		\
	ICE_M(0x3FFUL, VIRTCHNL_RXD_FLEX_LEN_HDR_S)
#define VIRTCHNL_RXD_FLEX_RSC_S		10
#define VIRTCHNL_RXD_FLEX_RSC_M		BIT_ULL(VIRTCHNL_RXD_FLEX_RSC_S)
#define VIRTCHNL_RXD_FLEX_SPH_S		11
#define VIRTCHNL_RXD_FLEX_SPH_M		BIT_ULL(VIRTCHNL_RXD_FLEX_SPH_S)
#define VIRTCHNL_RXD_FLEX_MISS_S		12
#define VIRTCHNL_RXD_FLEX_MISS_M		\
	BIT_ULL(VIRTCHNL_RXD_FLEX_MISS_S)
#define VIRTCHNL_RXD_FLEX_FF1_S		13
#define VIRTCHNL_RXD_FLEX_FF1_M		ICE_M(0x7UL, VIRTCHNL_RXD_FLEX_FF1_M)

/* For virtchnl_singleq_base_rx_legacy desc members */
#define VIRTCHNL_RXD_QW1_LEN_SPH_S	63
#define VIRTCHNL_RXD_QW1_LEN_SPH_M	BIT_ULL(VIRTCHNL_RXD_QW1_LEN_SPH_S)
#define VIRTCHNL_RXD_QW1_LEN_HBUF_S	52
#define VIRTCHNL_RXD_QW1_LEN_HBUF_M	\
	ICE_M(0x7FFULL, VIRTCHNL_RXD_QW1_LEN_HBUF_S)
#define VIRTCHNL_RXD_QW1_LEN_PBUF_S	38
#define VIRTCHNL_RXD_QW1_LEN_PBUF_M	\
	ICE_M(0x3FFFULL, VIRTCHNL_RXD_QW1_LEN_PBUF_S)
#define VIRTCHNL_RXD_QW1_PTYPE_S	30
#define VIRTCHNL_RXD_QW1_PTYPE_M	\
	ICE_M(0xFFULL, VIRTCHNL_RXD_QW1_PTYPE_S)
#define VIRTCHNL_RXD_QW1_ERROR_S	19
#define VIRTCHNL_RXD_QW1_ERROR_M	\
	ICE_M(0xFFUL, VIRTCHNL_RXD_QW1_ERROR_S)
#define VIRTCHNL_RXD_QW1_STATUS_S	0
#define VIRTCHNL_RXD_QW1_STATUS_M	\
	ICE_M(0x7FFFFUL, VIRTCHNL_RXD_QW1_STATUS_S)

enum virtchnl_rx_flex_desc_status_error_0_qw1_bits {
	/* Note: These are predefined bit offsets */
	VIRTCHNL_RX_FLEX_DESC_STATUS0_DD_S = 0,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_EOF_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_HBO_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_L3L4P_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XSUM_IPE_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XSUM_L4E_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XSUM_EIPE_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_S,
};

enum virtchnl_rx_flex_desc_status_error_0_qw0_bits {
	VIRTCHNL_RX_FLEX_DESC_STATUS0_LPBK_S = 0,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_IPV6EXADD_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_RXE_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_CRCP_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_RSS_VALID_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_L2TAG1P_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XTRMD0_VALID_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_XTRMD1_VALID_S,
	VIRTCHNL_RX_FLEX_DESC_STATUS0_LAST /* this entry must be last!!! */
};

enum virtchnl_rx_flex_desc_status_error_1_bits {
	/* Note: These are predefined bit offsets */
	VIRTCHNL_RX_FLEX_DESC_STATUS1_RSVD_S = 0, /* 2 bits */
	VIRTCHNL_RX_FLEX_DESC_STATUS1_ATRAEFAIL_S = 2,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_L2TAG2P_S = 3,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_XTRMD2_VALID_S = 4,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_XTRMD3_VALID_S = 5,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_XTRMD4_VALID_S = 6,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_XTRMD5_VALID_S = 7,
	VIRTCHNL_RX_FLEX_DESC_STATUS1_LAST /* this entry must be last!!! */
};

enum virtchnl_rx_base_desc_status_bits {
	/* Note: These are predefined bit offsets */
	VIRTCHNL_RX_BASE_DESC_STATUS_DD_S		= 0,
	VIRTCHNL_RX_BASE_DESC_STATUS_EOF_S		= 1,
	VIRTCHNL_RX_BASE_DESC_STATUS_L2TAG1P_S	= 2,
	VIRTCHNL_RX_BASE_DESC_STATUS_L3L4P_S	= 3,
	VIRTCHNL_RX_BASE_DESC_STATUS_CRCP_S		= 4,
	VIRTCHNL_RX_BASE_DESC_STATUS_RSVD_S		= 5, /* 3 BITS */
	VIRTCHNL_RX_BASE_DESC_STATUS_EXT_UDP_0_S	= 8,
	VIRTCHNL_RX_BASE_DESC_STATUS_UMBCAST_S	= 9, /* 2 BITS */
	VIRTCHNL_RX_BASE_DESC_STATUS_FLM_S		= 11,
	VIRTCHNL_RX_BASE_DESC_STATUS_FLTSTAT_S	= 12, /* 2 BITS */
	VIRTCHNL_RX_BASE_DESC_STATUS_LPBK_S		= 14,
	VIRTCHNL_RX_BASE_DESC_STATUS_IPV6EXADD_S	= 15,
	VIRTCHNL_RX_BASE_DESC_STATUS_RSVD1_S	= 16, /* 2 BITS */
	VIRTCHNL_RX_BASE_DESC_STATUS_INT_UDP_0_S	= 18,
	VIRTCHNL_RX_BASE_DESC_STATUS_LAST /* this entry must be last!!! */
};

enum virtchnl_rx_desc_fltstat_values {
	VIRTCHNL_RX_DESC_FLTSTAT_NO_DATA	= 0,
	VIRTCHNL_RX_DESC_FLTSTAT_RSV_FD_ID	= 1, /* 16byte desc? FD_ID : RSV */
	VIRTCHNL_RX_DESC_FLTSTAT_RSV	= 2,
	VIRTCHNL_RX_DESC_FLTSTAT_RSS_HASH	= 3,
};

enum virtchnl_rx_base_desc_error_bits {
	/* Note: These are predefined bit offsets */
	VIRTCHNL_RX_BASE_DESC_ERROR_RXE_S		= 0,
	VIRTCHNL_RX_BASE_DESC_ERROR_ATRAEFAIL_S	= 1,
	VIRTCHNL_RX_BASE_DESC_ERROR_HBO_S		= 2,
	VIRTCHNL_RX_BASE_DESC_ERROR_L3L4E_S		= 3, /* 3 BITS */
	VIRTCHNL_RX_BASE_DESC_ERROR_IPE_S		= 3,
	VIRTCHNL_RX_BASE_DESC_ERROR_L4E_S		= 4,
	VIRTCHNL_RX_BASE_DESC_ERROR_EIPE_S		= 5,
	VIRTCHNL_RX_BASE_DESC_ERROR_OVERSIZE_S	= 6,
	VIRTCHNL_RX_BASE_DESC_ERROR_RSVD_S		= 7
};

/* Receive Descriptors */
/* splitq buf
 |                                       16|                   0|
 ----------------------------------------------------------------
 | RSV                                     | Buffer ID          |
 ----------------------------------------------------------------
 | Rx packet buffer adresss                                     |
 ----------------------------------------------------------------
 | Rx header buffer adresss                                     |
 ----------------------------------------------------------------
 | RSV                                                          |
 ----------------------------------------------------------------
 |                                                             0|
 */
struct virtchnl_splitq_rx_buf_desc {
	struct {
		__le16  buf_id; /* Buffer Identifier */
		__le16  rsvd0;
		__le32  rsvd1;
	} qword0;
	__le64  pkt_addr; /* Packet buffer address */
	__le64  hdr_addr; /* Header buffer address */
	__le64  rsvd2;
}; /* read used with buffer queues*/

/* singleq buf
 |                                                             0|
 ----------------------------------------------------------------
 | Rx packet buffer adresss                                     |
 ----------------------------------------------------------------
 | Rx header buffer adresss                                     |
 ----------------------------------------------------------------
 | RSV                                                          |
 ----------------------------------------------------------------
 | RSV                                                          |
 ----------------------------------------------------------------
 |                                                             0|
 */
struct virtchnl_singleq_rx_buf_desc {
	__le64  pkt_addr; /* Packet buffer address */
	__le64  hdr_addr; /* Header buffer address */
	__le64  rsvd1;
	__le64  rsvd2;
}; /* read used with buffer queues*/

union virtchnl_rx_buf_desc {
	struct virtchnl_singleq_rx_buf_desc		read;
	struct virtchnl_splitq_rx_buf_desc		split_rd;
};

/* (0x00) singleq wb(compl) */
struct virtchnl_singleq_base_rx_desc {
	struct {
		struct {
			__le16 mirroring_status;
			__le16 l2tag1;
		} lo_dword;
		union {
			__le32 rss; /* RSS Hash */
			__le32 fd_id; /* Flow Director filter id */
		} hi_dword;
	} qword0;
	struct {
		/* status/error/PTYPE/length */
		__le64 status_error_ptype_len;
	} qword1;
	struct {
		__le16 ext_status; /* extended status */
		__le16 rsvd;
		__le16 l2tag2_1;
		__le16 l2tag2_2;
	} qword2;
	struct {
		__le32 reserved;
		__le32 fd_id;
	} qword3;
}; /* writeback */

/* (0x01) singleq flex compl */
struct virtchnl_rx_flex_desc {
	/* Qword 0 */
	u8 rxdid; /* descriptor builder profile id */
	u8 mir_id_umb_cast; /* mirror=[5:0], umb=[7:6] */
	__le16 ptype_flex_flags0; /* ptype=[9:0], ff0=[15:10] */
	__le16 pkt_len; /* [15:14] are reserved */
	__le16 hdr_len_sph_flex_flags1; /* header=[10:0] */
					/* sph=[11:11] */
					/* ff1/ext=[15:12] */

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le16 flex_meta0;
	__le16 flex_meta1;

	/* Qword 2 */
	__le16 status_error1;
	u8 flex_flags2;
	u8 time_stamp_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le16 flex_meta2;
	__le16 flex_meta3;
	union {
		struct {
			__le16 flex_meta4;
			__le16 flex_meta5;
		} flex;
		__le32 ts_high;
	} flex_ts;
};

/* (0x02) */
struct virtchnl_rx_flex_desc_nic {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le32 rss_hash;

	/* Qword 2 */
	__le16 status_error1;
	u8 flexi_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le32 flow_id;
	union {
		struct {
			__le16 rsvd;
			__le16 flow_id_ipv6;
		} flex;
		__le32 ts_high;
	} flex_ts;
};

/* Rx Flex Descriptor Switch Profile
 * RxDID Profile Id 3
 * Flex-field 0: Source Vsi
 */
struct virtchnl_rx_flex_desc_sw {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le16 src_vsi; /* [10:15] are reserved */
	__le16 flex_md1_rsvd;

	/* Qword 2 */
	__le16 status_error1;
	u8 flex_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le32 rsvd; /* flex words 2-3 are reserved */
	__le32 ts_high;
};

/* Rx Flex Descriptor NIC VEB Profile
 * RxDID Profile Id 4
 * Flex-field 0: Destination Vsi
 */
struct virtchnl_rx_flex_desc_nic_veb_dbg {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le16 dst_vsi; /* [0:12]: destination vsi */
			/* 13: vsi valid bit */
			/* [14:15] are reserved */
	__le16 flex_field_1;

	/* Qword 2 */
	__le16 status_error1;
	u8 flex_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le32 rsvd; /* flex words 2-3 are reserved */
	__le32 ts_high;
};

/* Rx Flex Descriptor NIC ACL Profile
 * RxDID Profile Id 5
 * Flex-field 0: ACL Counter 0
 * Flex-field 1: ACL Counter 1
 * Flex-field 2: ACL Counter 2
 */
struct virtchnl_rx_flex_desc_nic_acl_dbg {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le16 acl_ctr0;
	__le16 acl_ctr1;

	/* Qword 2 */
	__le16 status_error1;
	u8 flex_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le16 acl_ctr2;
	__le16 rsvd; /* flex words 2-3 are reserved */
	__le32 ts_high;
};

/* Rx Flex Descriptor NIC Profile
 * RxDID Profile Id 6
 * Flex-field 0: RSS hash lower 16-bits
 * Flex-field 1: RSS hash upper 16-bits
 * Flex-field 2: Flow Id lower 16-bits
 * Flex-field 3: Source Vsi
 * Flex-field 4: reserved, Vlan id taken from L2Tag
 */
struct virtchnl_rx_flex_desc_nic_2 {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le32 rss_hash;

	/* Qword 2 */
	__le16 status_error1;
	u8 flexi_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le16 flow_id;
	__le16 src_vsi;
	union {
		struct {
			__le16 rsvd;
			__le16 flow_id_ipv6;
		} flex;
		__le32 ts_high;
	} flex_ts;
};

/* Rx Flex Descriptor Advanced (Split Queue Model)
 * RxDID Profile Id 7
 */
struct virtchnl_rx_flex_desc_adv {
	/* Qword 0 */
	u8 rxdid_ucast; /* profile_id=[3:0] */
			/* rsvd=[5:4] */
			/* ucast=[7:6] */
	u8 status_err0_qw0;
	__le16 ptype_err_fflags0;	/* ptype=[9:0] */
					/* ip_hdr_err=[10:10] */
					/* udp_len_err=[11:11] */
					/* ff0=[15:12] */
	__le16 pktlen_gen_bufq_id;	/* plen=[13:0] */
					/* gen=[14:14]  only in splitq */
					/* bufq_id=[15:15] only in splitq */
	__le16 hdrlen_flags;		/* header=[9:0] */
					/* rsc=[10:10] only in splitq */
					/* sph=[11:11] only in splitq */
					/* ext_udp_0=[12:12] */
					/* int_udp_0=[13:13] */
					/* trunc_mirr=[14:14] */
					/* miss_prepend=[15:15] */
	/* Qword 1 */
	u8 status_err0_qw1;
	u8 status_err1;
	u8 fflags1;
	u8 ts_low;
	__le16 fmd0;
	__le16 fmd1;
	/* Qword 2 */
	__le16 fmd2;
	u8 fflags2;
	u8 hash3;
	__le16 fmd3;
	__le16 fmd4;
	/* Qword 3 */
	__le16 fmd5;
	__le16 fmd6;
	__le16 fmd7_0;
	__le16 fmd7_1;
}; /* writeback */

/* Rx Flex Descriptor Advanced (Split Queue Model) NIC Profile
 * RxDID Profile Id 8
 * Flex-field 0: BufferID
 * Flex-field 1: Raw checksum/L2TAG1/RSC Seg Len (determined by HW)
 * Flex-field 2: Hash[15:0]
 * Flex-flags 2: Hash[23:16]
 * Flex-field 3: L2TAG2
 * Flex-field 5: L2TAG1
 * Flex-field 7: Timestamp (upper 32 bits)
 */
struct virtchnl_rx_flex_desc_adv_nic_3 {
	/* Qword 0 */
	u8 rxdid_ucast; /* profile_id=[3:0] */
			/* rsvd=[5:4] */
			/* ucast=[7:6] */
	u8 status_err0_qw0;
	__le16 ptype_err_fflags0;	/* ptype=[9:0] */
					/* ip_hdr_err=[10:10] */
					/* udp_len_err=[11:11] */
					/* ff0=[15:12] */
	__le16 pktlen_gen_bufq_id;	/* plen=[13:0] */
					/* gen=[14:14]  only in splitq */
					/* bufq_id=[15:15] only in splitq */
	__le16 hdrlen_flags;		/* header=[9:0] */
					/* rsc=[10:10] only in splitq */
					/* sph=[11:11] only in splitq */
					/* ext_udp_0=[12:12] */
					/* int_udp_0=[13:13] */
					/* trunc_mirr=[14:14] */
					/* miss_prepend=[15:15] */
	/* Qword 1 */
	u8 status_err0_qw1;
	u8 status_err1;
	u8 fflags1;
	u8 ts_low;
	__le16 buf_id; /* only in splitq */
	union {
		__le16 raw_cs;
		__le16 l2tag1;
		__le16 rscseglen;
	} misc;
	/* Qword 2 */
	__le16 hash1;
	union {
		u8 fflags2;
		u8 mirrorid;
		u8 hash2;
	} ff2_mirrid_hash2;
	u8 hash3;
	__le16 l2tag2;
	__le16 fmd4;
	/* Qword 3 */
	__le16 l2tag1;
	__le16 fmd6;
	__le32 ts_high;
}; /* writeback */

union virtchnl_rx_desc {
	struct virtchnl_singleq_rx_buf_desc	read;
	struct virtchnl_singleq_base_rx_desc	base_wb;
	struct virtchnl_rx_flex_desc		flex_wb;
	struct virtchnl_rx_flex_desc_adv	flex_wb_adv;
};

#endif /* _VIRTCHNL_LAN_DESC_H_ */
