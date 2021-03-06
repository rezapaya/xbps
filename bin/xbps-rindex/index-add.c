/*-
 * Copyright (c) 2012-2013 Juan Romero Pardines.
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

#include <sys/stat.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <assert.h>
#include <fcntl.h>

#include <xbps.h>
#include "defs.h"

/*
 * Adds a binary package into the index and removes old binary package
 * and entry when it's necessary.
 */
int
index_add(struct xbps_handle *xhp, int argc, char **argv, bool force)
{
	xbps_array_t filespkgar, pkg_files, pkg_links, pkg_cffiles;
	xbps_dictionary_t idx, idxfiles, newpkgd, newpkgfilesd, curpkgd;
	xbps_object_t obj, fileobj;
	struct xbps_repo *repo;
	struct stat st;
	const char *oldpkgver, *arch, *oldarch;
	char *pkgver, *pkgname, *sha256, *repodir, *buf;
	char *tmprepodir;
	unsigned int x;
	int i, rv, ret = 0;
	bool flush = false, found = false;

	idx = idxfiles = newpkgd = newpkgfilesd = curpkgd = NULL;

	if ((tmprepodir = strdup(argv[0])) == NULL)
		return ENOMEM;

	/*
	 * Read the repository data or create index dictionaries otherwise.
	 */
	repodir = dirname(tmprepodir);

	repo = xbps_repo_open(xhp, repodir);
	if (repo != NULL) {
		idx = xbps_repo_get_plist(repo, XBPS_PKGINDEX);
		idxfiles = xbps_repo_get_plist(repo, XBPS_PKGINDEX_FILES);
	}
	if (idx == NULL)
		idx = xbps_dictionary_create();
	if (idxfiles == NULL)
		idxfiles = xbps_dictionary_create();
	if (repo != NULL)
		xbps_repo_close(repo);

	/*
	 * Process all packages specified in argv.
	 */
	for (i = 0; i < argc; i++) {
		/*
		 * Read metadata props plist dictionary from binary package.
		 */
		newpkgd = xbps_get_pkg_plist_from_binpkg(argv[i],
		    "./props.plist");
		if (newpkgd == NULL) {
			fprintf(stderr, "failed to read %s metadata for `%s',"
			    " skipping!\n", XBPS_PKGPROPS, argv[i]);
			continue;
		}
		xbps_dictionary_get_cstring_nocopy(newpkgd, "architecture",
		    &arch);
		xbps_dictionary_get_cstring(newpkgd, "pkgver", &pkgver);
		if (!xbps_pkg_arch_match(xhp, arch, NULL)) {
			fprintf(stderr, "index: ignoring %s, unmatched "
			    "arch (%s)\n", pkgver, arch);
			xbps_object_release(newpkgd);
			continue;
		}
		pkgname = xbps_pkg_name(pkgver);
		assert(pkgname);
		/*
		 * Check if this package exists already in the index, but first
		 * checking the version. If current package version is greater
		 * than current registered package, update the index; otherwise
		 * pass to the next one.
		 */
		curpkgd = xbps_dictionary_get(idx, pkgname);
		if (curpkgd == NULL) {
			if (errno && errno != ENOENT) {
				free(pkgver);
				free(pkgname);
				return errno;
			}
		} else if (!force) {
			/* Only check version if !force */
			xbps_dictionary_get_cstring_nocopy(curpkgd,
			    "pkgver", &oldpkgver);
			xbps_dictionary_get_cstring_nocopy(curpkgd,
			    "architecture", &oldarch);
			ret = xbps_cmpver(pkgver, oldpkgver);
			if (ret <= 0) {
				/* Same version or index version greater */
				fprintf(stderr, "index: skipping `%s' "
				    "(%s), already registered.\n",
				    pkgver, arch);
				xbps_object_release(newpkgd);
				free(pkgver);
				free(pkgname);
				continue;
			}
			/*
			 * Current package version is greater than
			 * index version.
			 */
			buf = xbps_xasprintf("`%s' (%s)", oldpkgver, oldarch);
			xbps_dictionary_remove(idx, pkgname);
			printf("index: removed obsolete entry %s.\n", buf);
			free(buf);
		}
		/*
		 * We have the dictionary now, add the required
		 * objects for the index.
		 */
		if ((sha256 = xbps_file_hash(argv[i])) == NULL) {
			free(pkgver);
			free(pkgname);
			return errno;
		}
		if (!xbps_dictionary_set_cstring(newpkgd, "filename-sha256",
		    sha256)) {
			free(pkgver);
			free(pkgname);
			return errno;
		}

		free(sha256);
		if (stat(argv[i], &st) == -1) {
			free(pkgver);
			free(pkgname);
			return errno;
		}

		if (!xbps_dictionary_set_uint64(newpkgd, "filename-size",
		    (uint64_t)st.st_size)) {
			free(pkgver);
			free(pkgname);
			return errno;
		}
		/*
		 * Remove obsolete package objects.
		 */
		xbps_dictionary_remove(newpkgd, "archive-compression-type");
		xbps_dictionary_remove(newpkgd, "build-date");
		xbps_dictionary_remove(newpkgd, "build_date");
		xbps_dictionary_remove(newpkgd, "conf_files");
		xbps_dictionary_remove(newpkgd, "filename");
		xbps_dictionary_remove(newpkgd, "homepage");
		xbps_dictionary_remove(newpkgd, "license");
		xbps_dictionary_remove(newpkgd, "maintainer");
		xbps_dictionary_remove(newpkgd, "packaged-with");
		xbps_dictionary_remove(newpkgd, "source-revisions");
		xbps_dictionary_remove(newpkgd, "long_desc");
		xbps_dictionary_remove(newpkgd, "pkgname");
		xbps_dictionary_remove(newpkgd, "version");
		/*
		 * Add new pkg dictionary into the index.
		 */
		if (!xbps_dictionary_set(idx, pkgname, newpkgd)) {
			free(pkgname);
			return EINVAL;
		}
		flush = true;
		printf("index: added `%s' (%s).\n", pkgver, arch);
		/*
		 * Add new pkg dictionary into the index-files.
		 */
		found = false;
		newpkgfilesd = xbps_get_pkg_plist_from_binpkg(argv[i],
				"./files.plist");
		if (newpkgfilesd == NULL) {
			free(pkgver);
			free(pkgname);
			return EINVAL;
		}

		/* Find out if binary pkg stored in index contain any file */
		pkg_cffiles = xbps_dictionary_get(newpkgfilesd, "conf_files");
		if (xbps_array_count(pkg_cffiles))
			found = true;
		else
			pkg_cffiles = NULL;

		pkg_files = xbps_dictionary_get(newpkgfilesd, "files");
		if (xbps_array_count(pkg_files))
			found = true;
		else
			pkg_files = NULL;

		pkg_links = xbps_dictionary_get(newpkgfilesd, "links");
		if (xbps_array_count(pkg_links))
			found = true;
		else
			pkg_links = NULL;

		/* If pkg does not contain any file, ignore it */
		if (!found) {
			xbps_object_release(newpkgfilesd);
			xbps_object_release(newpkgd);
			free(pkgver);
			free(pkgname);
			continue;
		}
		/* create pkg files array */
		filespkgar = xbps_array_create();
		assert(filespkgar);

		/* add conf_files in pkg files array */
		if (pkg_cffiles != NULL) {
			for (x = 0; x < xbps_array_count(pkg_cffiles); x++) {
				obj = xbps_array_get(pkg_cffiles, x);
				fileobj = xbps_dictionary_get(obj, "file");
				xbps_array_add(filespkgar, fileobj);
			}
		}
		/* add files array in pkg array */
		if (pkg_files != NULL) {
			for (x = 0; x < xbps_array_count(pkg_files); x++) {
				obj = xbps_array_get(pkg_files, x);
				fileobj = xbps_dictionary_get(obj, "file");
				xbps_array_add(filespkgar, fileobj);
			}
		}
		/* add links array in pkgd */
		if (pkg_links != NULL) {
			for (x = 0; x < xbps_array_count(pkg_links); x++) {
				obj = xbps_array_get(pkg_links, x);
				fileobj = xbps_dictionary_get(obj, "file");
				xbps_array_add(filespkgar, fileobj);
			}
		}
		xbps_object_release(newpkgfilesd);

		/* add pkg files array into index-files */
		xbps_dictionary_set(idxfiles, pkgver, filespkgar);
		xbps_object_release(filespkgar);

		printf("index-files: added `%s' (%s)\n", pkgver, arch);
		xbps_object_release(newpkgd);
		free(pkgver);
		free(pkgname);
	}
	/*
	 * Generate repository data file.
	 */
	if (flush) {
		rv = repodata_flush(xhp, repodir, idx, idxfiles);
		if (rv != 0)
			return rv;
	}
	printf("index: %u packages registered.\n",
	    xbps_dictionary_count(idx));
	printf("index-files: %u packages registered.\n",
	    xbps_dictionary_count(idxfiles));

	xbps_object_release(idx);
	xbps_object_release(idxfiles);

	return 0;
}
