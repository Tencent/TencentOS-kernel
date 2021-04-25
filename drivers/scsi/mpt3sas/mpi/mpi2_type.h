/*
 *  Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 *
 *           Name:  mpi2_type.h
 *          Title:  MPI basic type definitions
 *  Creation Date:  August 16, 2006
 *
 *    mpi2_type.h Version:  02.00.01
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 *  11-18-14  02.00.01  Updated copyright information.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_TYPE_H
#define MPI2_TYPE_H


/*******************************************************************************
 * Define MPI2_POINTER if it hasn't already been defined. By default
 * MPI2_POINTER is defined to be a near pointer. MPI2_POINTER can be defined as
 * a far pointer by defining MPI2_POINTER as "far *" before this header file is
 * included.
 */
#ifndef MPI2_POINTER
#define MPI2_POINTER     *
#endif

#ifdef MPT3SAS_BASE_H_INCLUDED

/*****************************************************************************
 *
 *            Basic Types
 *
 *****************************************************************************/

typedef u8 U8;
typedef __le16 U16;
typedef __le32 U32;
typedef __le64 U64 __attribute__((aligned(4)));

/*****************************************************************************
 *
 *           Pointer Types
 *
 *****************************************************************************/

typedef U8      *PU8;
typedef U16     *PU16;
typedef U32     *PU32;
typedef U64     *PU64;

#endif

/* the basic types may have already been included by mpi_type.h */
#if !defined(MPI_TYPE_H) && !defined(MPT3SAS_BASE_H_INCLUDED)
/*****************************************************************************
*
*               Basic Types
*
*****************************************************************************/

typedef signed   char   S8;
typedef unsigned char   U8;
typedef signed   short  S16;
typedef unsigned short  U16;


#if defined(unix) || defined(__arm) || defined(ALPHA) || defined(__PPC__) || defined(__ppc)

    typedef signed   int   S32;
    typedef unsigned int   U32;

#else

    typedef signed   long  S32;
    typedef unsigned long  U32;

#endif


typedef struct _S64
{
    U32          Low;
    S32          High;
} S64;

typedef struct _U64
{
    U32          Low;
    U32          High;
} U64;


/*****************************************************************************
*
*               Pointer Types
*
*****************************************************************************/

typedef S8      *PS8;
typedef U8      *PU8;
typedef S16     *PS16;
typedef U16     *PU16;
typedef S32     *PS32;
typedef U32     *PU32;
typedef S64     *PS64;
typedef U64     *PU64;

#endif

#endif

