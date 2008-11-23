/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008 WiredTiger Software.
 *	All rights reserved.
 *
 * $Id$
 */

#include "wt_internal.h"

/*
 * __wt_calloc --
 *	ANSI calloc function.
 */
int
__wt_calloc(IENV *ienv, size_t number, size_t size, void *retp)
{
	void *p;

	/*
	 * !!!
	 * This function MUST handle a NULL IENV structure reference.
	 */

	if ((p = calloc(number, size)) == NULL)
		return (WT_ERROR);

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_malloc --
 *	ANSI malloc function.
 */
int
__wt_malloc(IENV *ienv, size_t bytes_to_allocate, void *retp)
{
	void *p;

	/*
	 * !!!
	 * This function MUST handle a NULL IENV structure reference.
	 */

	WT_ASSERT(ienv, bytes_to_allocate != 0);

	if ((p = malloc(bytes_to_allocate)) == NULL)
		return (WT_ERROR);

#ifdef HAVE_DEBUG
	memset(p, OVERWRITE_BYTE, bytes_to_allocate);
#endif

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_realloc --
 *	ANSI realloc function.
 */
int
__wt_realloc(IENV *ienv, size_t bytes_to_allocate, void *retp)
{
	void *p;

	/*
	 * !!!
	 * This function MUST handle a NULL IENV structure reference.
	 */

	WT_ASSERT(ienv, bytes_to_allocate != 0);

	p = *(void **)retp;
	if ((p = realloc(p, bytes_to_allocate)) == NULL)
		return (WT_ERROR);

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_strdup --
 *	ANSI strdup function.
 */
int
__wt_strdup(IENV *ienv, const char *str, void *retp)
{
	size_t len;
	int ret;
	void *p;

	/*
	 * !!!
	 * This function MUST handle a NULL IENV structure reference.
	 */

	len = strlen(str) + 1;
	if ((ret = __wt_malloc(ienv, len, &p)) != 0)
		return (ret);

	memcpy(p, str, len);

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_free --
 *	ANSI free function.
 */
void
__wt_free(IENV *ienv, void *p)
{
	/*
	 * !!!
	 * This function MUST handle a NULL IENV structure reference.
	 */

	if (p != NULL)			/* ANSI C free semantics */
		free(p);
}
