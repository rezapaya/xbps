/*-
 * Copyright (c) 2009-2013 Juan Romero Pardines.
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

#include <sys/param.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <xbps.h>
#include "defs.h"

static int
pkgdb_cb(struct xbps_handle *xhp _unused,
		xbps_object_t obj,
		const char *key _unused,
		void *arg _unused,
		bool *done _unused)
{
	const char *pkgver;
	char *pkgname;
	int rv;

	xbps_dictionary_get_cstring_nocopy(obj, "pkgver", &pkgver);
	if (xhp->flags & XBPS_FLAG_VERBOSE)
		printf("Checking %s ...\n", pkgver);

	pkgname = xbps_pkg_name(pkgver);
	assert(pkgname);
	rv = check_pkg_integrity(xhp, obj, pkgname);
	free(pkgname);
	if (rv != 0)
		fprintf(stderr, "pkgdb failed for %s: %s\n",
		    pkgver, strerror(rv));

	return rv;
}

int
check_pkg_integrity_all(struct xbps_handle *xhp)
{
	int rv;

	/* force an update to get total pkg count */
	(void)xbps_pkgdb_update(xhp, false);

	rv = xbps_pkgdb_foreach_cb(xhp, pkgdb_cb, NULL);

	if ((rv = xbps_pkgdb_update(xhp, true)) != 0) {
		xbps_error_printf("failed to write pkgdb: %s\n",
		    strerror(rv));
		return rv;
	}
	return 0;
}

int
check_pkg_integrity(struct xbps_handle *xhp,
		    xbps_dictionary_t pkgd,
		    const char *pkgname)
{
	xbps_dictionary_t opkgd, propsd;
	const char *sha256;
	char *buf;
	int rv = 0;

	propsd = opkgd = NULL;

	/* find real pkg by name */
	opkgd = pkgd;
	if (opkgd == NULL) {
		if (((opkgd = xbps_pkgdb_get_pkg(xhp, pkgname)) == NULL) &&
		    ((opkgd = xbps_pkgdb_get_virtualpkg(xhp, pkgname)) == NULL)) {
			printf("Package %s is not installed.\n", pkgname);
			return 0;
		}
	}
	/*
	 * Check for props.plist metadata file.
	 */
	buf = xbps_xasprintf("%s/.%s.plist",  xhp->metadir, pkgname);
	propsd = xbps_dictionary_internalize_from_file(buf);
	free(buf);
	if (propsd == NULL) {
		xbps_error_printf("%s: unexistent metafile!\n", pkgname);
		return EINVAL;
	} else if (xbps_dictionary_count(propsd) == 0) {
		xbps_error_printf("%s: incomplete metadata file.\n", pkgname);
		xbps_object_release(propsd);
		return 1;
	}
	/*
	 * Check pkg metadata signature.
	 */
	xbps_dictionary_get_cstring_nocopy(opkgd, "metafile-sha256", &sha256);
	if (sha256 != NULL) {
		buf = xbps_xasprintf("%s/.%s.plist",
		    xhp->metadir, pkgname);
		rv = xbps_file_hash_check(buf, sha256);
		free(buf);
		if (rv == ERANGE) {
			xbps_object_release(propsd);
			fprintf(stderr, "%s: metadata file has been "
			    "modified!\n", pkgname);
			return 1;
		}
	}

#define RUN_PKG_CHECK(x, name, arg)				\
do {								\
	rv = check_pkg_##name(x, pkgname, arg);			\
	if (rv == -1) {						\
		xbps_error_printf("%s: the %s test "		\
		    "returned error!\n", pkgname, #name);	\
		return rv;					\
	}							\
} while (0)

	/* Execute pkg checks */
	RUN_PKG_CHECK(xhp, files, propsd);
	RUN_PKG_CHECK(xhp, symlinks, propsd);
	RUN_PKG_CHECK(xhp, rundeps, propsd);
	RUN_PKG_CHECK(xhp, unneeded, opkgd);

	xbps_object_release(propsd);

#undef RUN_PKG_CHECK

	if ((rv == 0) && (pkgd == NULL))
		(void)xbps_pkgdb_update(xhp, true);

	return 0;
}
