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
#include <pthread.h>
#include <fcntl.h>

#include <xbps.h>
#include "defs.h"

struct thread_data {
	pthread_t thread;
	xbps_dictionary_t idx;
	xbps_dictionary_t idxfiles;
	xbps_array_t result;
	xbps_array_t result_files;
	struct xbps_handle *xhp;
	unsigned int start;
	unsigned int end;
	int thread_num;
};

static void *
cleaner_thread(void *arg)
{
	xbps_object_t obj;
	xbps_dictionary_t pkgd;
	xbps_array_t array;
	struct thread_data *thd = arg;
	char *filen;
	const char *pkgver, *arch, *sha256;
	unsigned int i;

	/* process pkgs from start until end */
	array = xbps_dictionary_all_keys(thd->idx);

	for (i = thd->start; i < thd->end; i++) {
		obj = xbps_array_get(array, i);
		pkgd = xbps_dictionary_get_keysym(thd->idx, obj);
		xbps_dictionary_get_cstring_nocopy(pkgd, "architecture", &arch);
		xbps_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
		filen = xbps_xasprintf("%s.%s.xbps", pkgver, arch);
		xbps_dbg_printf(thd->xhp, "thread[%d] checking %s\n",
		    thd->thread_num, pkgver);
		if (access(filen, R_OK) == -1) {
			/*
			 * File cannot be read, might be permissions,
			 * broken or simply unexistent; either way, remove it.
			 */
			xbps_array_add_cstring_nocopy(thd->result, pkgver);
			free(filen);
			continue;
		}
		/*
		 * File can be read; check its hash.
		 */
		xbps_dictionary_get_cstring_nocopy(pkgd,
		    "filename-sha256", &sha256);
		if (xbps_file_hash_check(filen, sha256) != 0)
			xbps_array_add_cstring_nocopy(thd->result, pkgver);
		free(filen);
	}
	xbps_object_release(array);

	return NULL;
}

static void *
cleaner_files_thread(void *arg)
{
	xbps_object_t obj;
	xbps_array_t array;
	xbps_dictionary_t ipkgd;
	struct thread_data *thd = arg;
	const char *pkgver, *ipkgver;
	char *pkgname;
	unsigned int i;

	/* process pkgs from start until end */
	array = xbps_dictionary_all_keys(thd->idxfiles);

	for (i = thd->start; i < thd->end; i++) {
		obj = xbps_array_get(array, i);
		pkgver = xbps_dictionary_keysym_cstring_nocopy(obj);
		pkgname = xbps_pkg_name(pkgver);
		assert(pkgname);
		ipkgd = xbps_dictionary_get(thd->idx, pkgname);
		/* If pkg is not registered in index, remove it */
		if (ipkgd == NULL)
			xbps_array_add_cstring_nocopy(thd->result_files, pkgver);
		/* if another version is registered in index, remove it */
		else {
			xbps_dictionary_get_cstring_nocopy(ipkgd, "pkgver", &ipkgver);
			if (strcmp(ipkgver, pkgver))
				xbps_array_add_cstring_nocopy(thd->result_files, pkgver);
		}
		free(pkgname);
	}
	xbps_object_release(array);

	return NULL;
}

/*
 * Removes stalled pkg entries in repository's index.plist file, if any
 * binary package cannot be read (unavailable, not enough perms, etc).
 */
int
index_clean(struct xbps_handle *xhp, const char *repodir)
{
	struct xbps_repo *repo;
	struct thread_data *thd;
	xbps_dictionary_t idx, idxfiles;
	const char *keyname;
	char *pkgname;
	unsigned int x, pkgcount, slicecount;
	int i, maxthreads, rv = 0;
	bool flush = false;

	repo = xbps_repo_open(xhp, repodir);
	if (repo == NULL) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "index: cannot read repository data: %s\n", strerror(errno));
		return -1;
	}
	idx = xbps_repo_get_plist(repo, XBPS_PKGINDEX);
	idxfiles = xbps_repo_get_plist(repo, XBPS_PKGINDEX_FILES);
	xbps_repo_close(repo);
	if (idx == NULL || idxfiles == NULL) {
		fprintf(stderr, "incomplete repository data file!");
		return -1;
	}
	if (chdir(repodir) == -1) {
		fprintf(stderr, "index: cannot chdir to %s: %s\n",
		    repodir, strerror(errno));
		return -1;
	}
	printf("Cleaning `%s' index, please wait...\n", repodir);

	maxthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);
	thd = calloc(maxthreads, sizeof(*thd));

	slicecount = xbps_dictionary_count(idx) / maxthreads;
	pkgcount = 0;

	/* Setup threads to cleanup index and index-files */
	for (i = 0; i < maxthreads; i++) {
		thd[i].thread_num = i;
		thd[i].idx = idx;
		thd[i].result = xbps_array_create();
		thd[i].xhp = xhp;
		thd[i].start = pkgcount;
		if (i + 1 >= maxthreads)
			thd[i].end = xbps_dictionary_count(idx);
		else
			thd[i].end = pkgcount + slicecount;
		pthread_create(&thd[i].thread, NULL, cleaner_thread, &thd[i]);
		pkgcount += slicecount;
	}
	/* wait for all threads */
	for (i = 0; i < maxthreads; i++)
		pthread_join(thd[i].thread, NULL);

	/* Setup threads to cleanup index-files */
	slicecount = xbps_dictionary_count(idxfiles) / maxthreads;
	pkgcount = 0;

	for (i = 0; i < maxthreads; i++) {
		thd[i].thread_num = i;
		thd[i].idx = idx;
		thd[i].idxfiles = idxfiles;
		thd[i].result_files = xbps_array_create();
		thd[i].xhp = xhp;
		thd[i].start = pkgcount;
		if (i + 1 >= maxthreads)
			thd[i].end = xbps_dictionary_count(idxfiles);
		else
			thd[i].end = pkgcount + slicecount;
		pthread_create(&thd[i].thread, NULL, cleaner_files_thread, &thd[i]);
		pkgcount += slicecount;
	}
	/* wait for all threads */
	for (i = 0; i < maxthreads; i++)
		pthread_join(thd[i].thread, NULL);

	for (i = 0; i < maxthreads; i++) {
		for (x = 0; x < xbps_array_count(thd[i].result); x++) {
			xbps_array_get_cstring_nocopy(thd[i].result,
			    x, &keyname);
			printf("index: removed entry %s\n", keyname);
			pkgname = xbps_pkg_name(keyname);
			xbps_dictionary_remove(idx, pkgname);
			xbps_dictionary_remove(idxfiles, keyname);
			free(pkgname);
			flush = true;
		}
		for (x = 0; x < xbps_array_count(thd[i].result_files); x++) {
			xbps_array_get_cstring_nocopy(thd[i].result_files,
			    x, &keyname);
			printf("index-files: removed entry %s\n", keyname);
			xbps_dictionary_remove(idxfiles, keyname);
			flush = true;
		}
	}
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

	return rv;
}
