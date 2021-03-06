/*-
 * Copyright (c) 2008-2012 Juan Romero Pardines.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xbps_api_impl.h"

/**
 * @file lib/plist_find.c
 * @brief PropertyList generic routines
 * @defgroup plist PropertyList generic functions
 *
 * These functions manipulate plist files and objects shared by almost
 * all library functions.
 */
bool
xbps_match_virtual_pkg_in_dict(xbps_dictionary_t d,
			       const char *str,
			       bool bypattern)
{
	xbps_array_t provides;
	bool found = false;

	assert(xbps_object_type(d) == XBPS_TYPE_DICTIONARY);

	if ((provides = xbps_dictionary_get(d, "provides"))) {
		if (bypattern)
			found = xbps_match_pkgpattern_in_array(provides, str);
		else {
			if ((xbps_match_pkgname_in_array(provides, str)) ||
			    (xbps_match_pkgdep_in_array(provides, str)))
				return true;
		}
	}
	return found;
}

bool
xbps_match_any_virtualpkg_in_rundeps(xbps_array_t rundeps,
				     xbps_array_t provides)
{
	xbps_object_t obj, obj2;
	xbps_object_iterator_t iter, iter2;
	const char *vpkgver, *pkgpattern;
	char *tmp;

	iter = xbps_array_iterator(provides);
	assert(iter);

	while ((obj = xbps_object_iterator_next(iter))) {
		tmp = NULL;
		vpkgver = xbps_string_cstring_nocopy(obj);
		if (strchr(vpkgver, '_') == NULL) {
			tmp = xbps_xasprintf("%s_1", vpkgver);
			vpkgver = tmp;
		}
		iter2 = xbps_array_iterator(rundeps);
		assert(iter2);
		while ((obj2 = xbps_object_iterator_next(iter2))) {
			pkgpattern = xbps_string_cstring_nocopy(obj2);
			if (xbps_pkgpattern_match(vpkgver, pkgpattern)) {
				if (tmp != NULL)
					free(tmp);

				xbps_object_iterator_release(iter2);
				xbps_object_iterator_release(iter);
				return true;
			}
		}
		xbps_object_iterator_release(iter2);
		if (tmp != NULL)
			free(tmp);
	}
	xbps_object_iterator_release(iter);

	return false;
}

static bool
match_string_in_array(xbps_array_t array, const char *str, int mode)
{
	xbps_object_iterator_t iter;
	xbps_object_t obj;
	const char *pkgdep;
	char *curpkgname, *tmp;
	bool found = false;

	assert(xbps_object_type(array) == XBPS_TYPE_ARRAY);
	assert(str != NULL);

	iter = xbps_array_iterator(array);
	assert(iter);

	while ((obj = xbps_object_iterator_next(iter))) {
		tmp = NULL;
		if (mode == 0) {
			/* match by string */
			if (xbps_string_equals_cstring(obj, str)) {
				found = true;
				break;
			}
		} else if (mode == 1) {
			/* match by pkgname */
			pkgdep = xbps_string_cstring_nocopy(obj);
			if (strchr(pkgdep, '_') == NULL) {
				tmp = xbps_xasprintf("%s_1", pkgdep);
				curpkgname = xbps_pkg_name(tmp);
				free(tmp);
			} else {
				curpkgname = xbps_pkg_name(pkgdep);
			}
			if (curpkgname == NULL)
				break;
			if (strcmp(curpkgname, str) == 0) {
				free(curpkgname);
				found = true;
				break;
			}
			free(curpkgname);
		} else if (mode == 2) {
			/* match pkgpattern against pkgdep */
			pkgdep = xbps_string_cstring_nocopy(obj);
			if (strchr(pkgdep, '_') == NULL) {
				tmp = xbps_xasprintf("%s_1", pkgdep);
				pkgdep = tmp;
			}
			if (xbps_pkgpattern_match(pkgdep, str)) {
				if (tmp != NULL)
					free(tmp);
				found = true;
				break;
			}
			if (tmp != NULL)
				free(tmp);

		} else if (mode == 3) {
			/* match pkgdep against pkgpattern */
			pkgdep = xbps_string_cstring_nocopy(obj);
			if (strchr(pkgdep, '_') == NULL) {
				tmp = xbps_xasprintf("%s_1", pkgdep);
				pkgdep = tmp;
			}
			if (xbps_pkgpattern_match(str, pkgdep)) {
				if (tmp != NULL)
					free(tmp);
				found = true;
				break;
			}
			if (tmp != NULL)
				free(tmp);
		}
	}
	xbps_object_iterator_release(iter);

	return found;
}

bool
xbps_match_string_in_array(xbps_array_t array, const char *str)
{
	return match_string_in_array(array, str, 0);
}

bool
xbps_match_pkgname_in_array(xbps_array_t array, const char *pkgname)
{
	return match_string_in_array(array, pkgname, 1);
}

bool
xbps_match_pkgpattern_in_array(xbps_array_t array, const char *pattern)
{
	return match_string_in_array(array, pattern, 2);
}

bool
xbps_match_pkgdep_in_array(xbps_array_t array, const char *pkgver)
{
	return match_string_in_array(array, pkgver, 3);
}
