/*-
 * Copyright (c) 2008-2013 Juan Romero Pardines.
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
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include "xbps_api_impl.h"

static pthread_mutex_t cfg_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool cfg_vpkgs_init;

static xbps_dictionary_t
get_pkg_in_array(xbps_array_t array, const char *str, bool virtual)
{
	xbps_object_t obj = NULL;
	xbps_object_iterator_t iter;
	const char *pkgver;
	char *dpkgn;
	bool found = false;

	iter = xbps_array_iterator(array);
	assert(iter);

	while ((obj = xbps_object_iterator_next(iter))) {
		if (virtual) {
			/*
			 * Check if package pattern matches
			 * any virtual package version in dictionary.
			 */
			if (xbps_pkgpattern_version(str))
				found = xbps_match_virtual_pkg_in_dict(obj, str, true);
			else
				found = xbps_match_virtual_pkg_in_dict(obj, str, false);

			if (found)
				break;
		} else if (xbps_pkgpattern_version(str)) {
			/* match by pattern against pkgver */
			if (!xbps_dictionary_get_cstring_nocopy(obj,
			    "pkgver", &pkgver))
				continue;
			if (xbps_pkgpattern_match(pkgver, str)) {
				found = true;
				break;
			}
		} else if (xbps_pkg_version(str)) {
			/* match by exact pkgver */
			if (!xbps_dictionary_get_cstring_nocopy(obj,
			    "pkgver", &pkgver))
				continue;
			if (strcmp(str, pkgver) == 0) {
				found = true;
				break;
			}
		} else {
			/* match by pkgname */
			if (!xbps_dictionary_get_cstring_nocopy(obj,
			    "pkgver", &pkgver))
				continue;
			dpkgn = xbps_pkg_name(pkgver);
			assert(dpkgn);
			if (strcmp(dpkgn, str) == 0) {
				free(dpkgn);
				found = true;
				break;
			}
			free(dpkgn);
		}
	}
	xbps_object_iterator_release(iter);

	return found ? obj : NULL;
}

xbps_dictionary_t HIDDEN
xbps_find_pkg_in_array(xbps_array_t a, const char *s)
{
	assert(xbps_object_type(a) == XBPS_TYPE_ARRAY);
	assert(s);

	return get_pkg_in_array(a, s, false);
}

xbps_dictionary_t HIDDEN
xbps_find_virtualpkg_in_array(struct xbps_handle *x,
			      xbps_array_t a,
			      const char *s)
{
	xbps_dictionary_t pkgd;
	const char *vpkg;
	bool bypattern = false;

	assert(x);
	assert(xbps_object_type(a) == XBPS_TYPE_ARRAY);
	assert(s);

	if (xbps_pkgpattern_version(s))
		bypattern = true;

	if ((vpkg = vpkg_user_conf(x, s, bypattern))) {
		if ((pkgd = get_pkg_in_array(a, vpkg, true)))
			return pkgd;
	}

	return get_pkg_in_array(a, s, true);
}

static xbps_dictionary_t
match_pkg_by_pkgver(xbps_dictionary_t repod, const char *p)
{
	xbps_dictionary_t d = NULL;
	const char *pkgver;
	char *pkgname;

	/* exact match by pkgver */
	if ((pkgname = xbps_pkg_name(p)) == NULL)
		return NULL;

	d = xbps_dictionary_get(repod, pkgname);
	if (d) {
		xbps_dictionary_get_cstring_nocopy(d, "pkgver", &pkgver);
		if (strcmp(pkgver, p))
			d = NULL;
	}

	free(pkgname);
	return d;
}

static xbps_dictionary_t
match_pkg_by_pattern(xbps_dictionary_t repod, const char *p)
{
	xbps_dictionary_t d = NULL;
	const char *pkgver;
	char *pkgname;

	/* match by pkgpattern in pkgver */
	if ((pkgname = xbps_pkgpattern_name(p)) == NULL) {
		if ((pkgname = xbps_pkg_name(p))) {
			free(pkgname);
			return match_pkg_by_pkgver(repod, p);
		}
		return NULL;
	}

	d = xbps_dictionary_get(repod, pkgname);
	if (d) {
		xbps_dictionary_get_cstring_nocopy(d, "pkgver", &pkgver);
		assert(pkgver);
		if (!xbps_pkgpattern_match(pkgver, p))
			d = NULL;
	}

	free(pkgname);
	return d;
}

static void
config_inject_vpkgs(struct xbps_handle *xh)
{
	DIR *dirp;
	struct dirent *dp;
	char *ext, *vpkgdir;
	FILE *fp;

	if (strcmp(xh->rootdir, "/"))
		vpkgdir = xbps_xasprintf("%s/etc/xbps/virtualpkg.d",
		    xh->rootdir);
	else
		vpkgdir = strdup("/etc/xbps/virtualpkg.d");

	if ((dirp = opendir(vpkgdir)) == NULL) {
		xbps_dbg_printf(xh, "cannot access to %s: %s\n",
		    vpkgdir, strerror(errno));
		free(vpkgdir);
		return;
	}

	while ((dp = readdir(dirp)) != NULL) {
		if ((strcmp(dp->d_name, "..") == 0) ||
		    (strcmp(dp->d_name, ".") == 0))
			continue;
		/* only process .conf files, ignore something else */
		if ((ext = strrchr(dp->d_name, '.')) == NULL)
			continue;
		if (strcmp(ext, ".conf") == 0) {
			char *path;

			path = xbps_xasprintf("%s/%s", vpkgdir, dp->d_name);
			fp = fopen(path, "r");
			assert(fp);
			if (cfg_parse_fp(xh->cfg, fp) != 0) {
				xbps_error_printf("Failed to parse "
				    "vpkg conf file %s:\n", dp->d_name);
			}
			fclose(fp);
			xbps_dbg_printf(xh, "Injected vpkgs from %s\n", path);
			free(path);
		}
	}
	closedir(dirp);
	free(vpkgdir);
	cfg_vpkgs_init = true;
}

const char HIDDEN *
vpkg_user_conf(struct xbps_handle *xhp,
	       const char *vpkg,
	       bool bypattern)
{
	const char *vpkgver, *pkg = NULL;
	char *vpkgname = NULL, *tmp;
	unsigned int i, j, cnt;

	if (xhp->cfg == NULL)
		return NULL;

	/* inject virtual packages from sysconfdir */
	pthread_mutex_lock(&cfg_mtx);
	if (!cfg_vpkgs_init)
		config_inject_vpkgs(xhp);
	pthread_mutex_unlock(&cfg_mtx);

	if ((cnt = cfg_size(xhp->cfg, "virtual-package")) == 0) {
		/* no virtual packages configured */
		return NULL;
	}

	for (i = 0; i < cnt; i++) {
		cfg_t *sec = cfg_getnsec(xhp->cfg, "virtual-package", i);
		for (j = 0; j < cfg_size(sec, "targets"); j++) {
			tmp = NULL;
			vpkgver = cfg_getnstr(sec, "targets", j);
			if (strchr(vpkgver, '_') == NULL) {
				tmp = xbps_xasprintf("%s_1", vpkgver);
				vpkgname = xbps_pkg_name(tmp);
				free(tmp);
			} else {
				vpkgname = xbps_pkg_name(vpkgver);
			}
			if (vpkgname == NULL)
				break;
			if (bypattern) {
				if (!xbps_pkgpattern_match(vpkgver, vpkg)) {
					free(vpkgname);
					continue;
				}
			} else {
				if (strcmp(vpkg, vpkgname)) {
					free(vpkgname);
					continue;
				}
			}
			/* virtual package matched in conffile */
			pkg = cfg_title(sec);
			xbps_dbg_printf(xhp,
			    "matched vpkg in conf `%s' for %s\n",
			    pkg, vpkg);
			free(vpkgname);
			break;
		}
	}
	return pkg;
}

xbps_dictionary_t
xbps_find_virtualpkg_in_dict(struct xbps_handle *xhp,
			     xbps_dictionary_t d,
			     const char *pkg)
{
	xbps_object_t obj;
	xbps_object_iterator_t iter;
	xbps_dictionary_t pkgd = NULL;
	const char *vpkg;
	bool bypattern = false;

	if (xbps_pkgpattern_version(pkg))
		bypattern = true;

	/* Try matching vpkg from configuration files */
	vpkg = vpkg_user_conf(xhp, pkg, bypattern);
	if (vpkg != NULL) {
		if (xbps_pkgpattern_version(vpkg))
			pkgd = match_pkg_by_pattern(d, vpkg);
		else if (xbps_pkg_version(vpkg))
			pkgd = match_pkg_by_pkgver(d, vpkg);
		else
			pkgd = xbps_dictionary_get(d, vpkg);

		if (pkgd)
			return pkgd;
	}

	/* ... otherwise match the first one in dictionary */
	iter = xbps_dictionary_iterator(d);
	assert(iter);

	while ((obj = xbps_object_iterator_next(iter))) {
		pkgd = xbps_dictionary_get_keysym(d, obj);
		if (xbps_match_virtual_pkg_in_dict(pkgd, pkg, bypattern)) {
			xbps_object_iterator_release(iter);
			return pkgd;
		}
	}
	xbps_object_iterator_release(iter);

	return NULL;
}

xbps_dictionary_t
xbps_find_pkg_in_dict(xbps_dictionary_t d, const char *pkg)
{
	xbps_dictionary_t pkgd = NULL;

	if (xbps_pkgpattern_version(pkg))
		pkgd = match_pkg_by_pattern(d, pkg);
	else if (xbps_pkg_version(pkg))
		pkgd = match_pkg_by_pkgver(d, pkg);
	else
		pkgd = xbps_dictionary_get(d, pkg);

	return pkgd;
}
