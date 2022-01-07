/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2021 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
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

#define LPFC_DRIVER_VERSION "12.8.542.34"

#ifndef BUILD_BRCMFCOE

#define LPFC_DRIVER_NAME		"lpfc"
#define LPFCMGMT_NAME			"lpfcmgmt"

/* Used for SLI 2/3 */
#define LPFC_SP_DRIVER_HANDLER_NAME	"lpfc:sp"
#define LPFC_FP_DRIVER_HANDLER_NAME	"lpfc:fp"

/* Used for SLI4 */
#define LPFC_DRIVER_HANDLER_NAME	"lpfc:"

#define LPFC_MODULE_DESC "Emulex LightPulse Fibre Channel SCSI driver " \
		LPFC_DRIVER_VERSION
#define LPFC_COPYRIGHT "Copyright (C) 2017-2021 Broadcom. All Rights " \
		"Reserved. The term \"Broadcom\" refers to Broadcom Inc. " \
		"and/or its subsidiaries."

#else

#define LPFC_DRIVER_NAME		"brcmfcoe"
#define LPFCMGMT_NAME			"brcmfcoemgmt"

/* Used for SLI 2/3 */
#define LPFC_SP_DRIVER_HANDLER_NAME	"brcmfcoe:sp"
#define LPFC_FP_DRIVER_HANDLER_NAME	"brcmfcoe:fp"

/* Used for SLI4 */
#define LPFC_DRIVER_HANDLER_NAME	"brcmfcoe:"

#define LPFC_MODULE_DESC "Emulex FCoE SCSI driver " \
		LPFC_DRIVER_VERSION
#define LPFC_COPYRIGHT "Copyright (C) 2017 Broadcom. All Rights Reserved. " \
		"The term \"Broadcom\" refers to Broadcom Limited " \
		"and/or its subsidiaries."
#endif
