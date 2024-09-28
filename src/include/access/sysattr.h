/*-------------------------------------------------------------------------
 *
 * sysattr.h
 *	  POSTGRES system attribute definitions.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/sysattr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYSATTR_H
#define SYSATTR_H

/* POLAR px */
#include "postgres.h"

#include "px/px_vars.h"

/*
 * Attribute numbers for the system-defined attributes
 */
#define SelfItemPointerAttributeNumber			(-1)
<<<<<<< HEAD
#define ObjectIdAttributeNumber					(-2)
#define MinTransactionIdAttributeNumber			(-3)
#define MinCommandIdAttributeNumber				(-4)
#define MaxTransactionIdAttributeNumber			(-5)
#define MaxCommandIdAttributeNumber				(-6)
#define TableOidAttributeNumber					(-7)
/* POLAR px */
#define PxWorkerIdAttributeNumber			    (-8)
#define RootSelfItemPointerAttributeNumber      (-9)
#define FirstLowInvalidHeapAttributeNumber      (-10)
/* POLAR end */
=======
#define MinTransactionIdAttributeNumber			(-2)
#define MinCommandIdAttributeNumber				(-3)
#define MaxTransactionIdAttributeNumber			(-4)
#define MaxCommandIdAttributeNumber				(-5)
#define TableOidAttributeNumber					(-6)
#define FirstLowInvalidHeapAttributeNumber		(-7)

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#endif							/* SYSATTR_H */
