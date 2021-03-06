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
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <xbps.h>
#include "defs.h"

static void __attribute__((noreturn))
usage(bool fail)
{
	fprintf(stdout,
	    "Usage: xbps-query [OPTIONS...] [PKGNAME]\n"
	    "\nOPTIONS\n"
	    " -C --config <file>       Full path to configuration file\n"
	    " -c --cachedir <dir>      Full path to cachedir\n"
	    " -D --defrepo <uri>       Default repository to be used if config not set\n"
	    " -d --debug               Debug mode shown to stderr\n"
	    " -h --help                Print help usage\n"
	    " -R --repository          Enable repository mode\n"
	    " -r --rootdir <dir>       Full path to rootdir\n"
	    " -V --version             Show XBPS version\n"
	    " -v --verbose             Verbose messages\n"
	    "\nMODE [only one mode may be specified]\n"
	    " -l --list-pkgs           List available packages\n"
	    " -L --list-repos          List working repositories\n"
	    " -H --list-hold-pkgs      List packages on hold state\n"
	    " -m --list-manual-pkgs    List packages installed explicitly\n"
	    " -O --list-orphans        List package orphans\n"
	    " -o --ownedby PATTERN(s)  Search for packages owning PATTERN(s)\n"
	    " -s --search PATTERN(s)   Search for packages matching PATTERN(s)\n"
	    " -f --files               Show files for PKGNAME\n"
	    " -p --property PROP,...   Show properties for PKGNAME\n"
	    " -x --deps                Show dependencies for PKGNAME (set it twice for a full dependency tree)\n"
	    " -X --revdeps             Show reverse dependencies for PKGNAME\n");

	exit(fail ? EXIT_FAILURE : EXIT_SUCCESS);
}

int
main(int argc, char **argv)
{
	const char *shortopts = "C:c:D:dfhHLlmOop:Rr:sVvXx";
	const struct option longopts[] = {
		{ "config", required_argument, NULL, 'C' },
		{ "cachedir", required_argument, NULL, 'c' },
		{ "defrepo", required_argument, NULL, 'D' },
		{ "debug", no_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ "list-repos", no_argument, NULL, 'L' },
		{ "list-pkgs", no_argument, NULL, 'l' },
		{ "list-hold-pkgs", no_argument, NULL, 'H' },
		{ "list-manual-pkgs", no_argument, NULL, 'm' },
		{ "list-orphans", no_argument, NULL, 'O' },
		{ "ownedby", no_argument, NULL, 'o' },
		{ "property", required_argument, NULL, 'p' },
		{ "repository-mode", no_argument, NULL, 'R' },
		{ "rootdir", required_argument, NULL, 'r' },
		{ "search", no_argument, NULL, 's' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "files", no_argument, NULL, 'f' },
		{ "deps", no_argument, NULL, 'x' },
		{ "revdeps", no_argument, NULL, 'X' },
		{ NULL, 0, NULL, 0 },
	};
	struct xbps_handle xh;
	const char *rootdir, *cachedir, *conffile, *props, *defrepo;
	int c, flags, rv, show_deps = 0;
	bool list_pkgs, list_repos, orphans, own;
	bool list_manual, list_hold, show_prop, show_files, show_rdeps;
	bool show, search, repo_mode, opmode, fulldeptree;

	rootdir = cachedir = conffile = defrepo = props = NULL;
	flags = rv = c = 0;
	list_pkgs = list_repos = list_hold = orphans = search = own = false;
	list_manual = show_prop = show_files = false;
	show = show_rdeps = fulldeptree = false;
	repo_mode = opmode = false;

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'C':
			conffile = optarg;
			break;
		case 'c':
			cachedir = optarg;
			break;
		case 'D':
			defrepo = optarg;
			break;
		case 'd':
			flags |= XBPS_FLAG_DEBUG;
			break;
		case 'f':
			show_files = opmode = true;
			break;
		case 'H':
			list_hold = true;
			break;
		case 'h':
			usage(false);
			/* NOTREACHED */
		case 'L':
			list_repos = true;
			break;
		case 'l':
			list_pkgs = true;
			break;
		case 'm':
			list_manual = true;
			break;
		case 'O':
			orphans = true;
			break;
		case 'o':
			own = opmode = true;
			break;
		case 'p':
			props = optarg;
			show_prop = opmode = true;
			break;
		case 'R':
			repo_mode = true;
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 's':
			search = opmode = true;
			break;
		case 'v':
			flags |= XBPS_FLAG_VERBOSE;
			break;
		case 'V':
			printf("%s\n", XBPS_RELVER);
			exit(EXIT_SUCCESS);
		case 'x':
			show_deps++;
			opmode = true;
			break;
		case 'X':
			show_rdeps = opmode = true;
			break;
		case '?':
			usage(true);
			/* NOTREACHED */
		}
	}
	if (!opmode && argc > optind)
		show = true;
	else if (argc == 1 || (opmode && (argc == optind)))
		usage(true);
	else if ((search || own) && (argc == optind))
		usage(true);

	/*
	 * Initialize libxbps.
	 */
	memset(&xh, 0, sizeof(xh));
	xh.rootdir = rootdir;
	xh.cachedir = cachedir;
	xh.conffile = conffile;
	xh.flags = flags;
	xh.repository = defrepo;

	if ((rv = xbps_init(&xh)) != 0) {
		xbps_error_printf("Failed to initialize libxbps: %s\n",
		    strerror(rv));
		exit(EXIT_FAILURE);
	}

	if (list_repos) {
		/* list repositories */
		rv = repo_list(&xh);

	} else if (list_hold) {
		/* list on hold pkgs */
		rv = xbps_pkgdb_foreach_cb(&xh, list_hold_pkgs, NULL);

	} else if (list_manual) {
		/* list manual pkgs */
		rv = xbps_pkgdb_foreach_cb(&xh, list_manual_pkgs, NULL);

	} else if (list_pkgs) {
		/* list available pkgs */
		rv = list_pkgs_pkgdb(&xh);

	} else if (orphans) {
		/* list pkg orphans */
		rv = list_orphans(&xh);

	} else if (own) {
		/* ownedby mode */
		if (repo_mode)
			rv = repo_ownedby(&xh, argc - optind, argv + optind);
		else
			rv = ownedby(&xh, argc - optind, argv + optind);

	} else if (search) {
		/* search mode */
		rv = repo_search(&xh, argc - optind, argv + optind);

	} else if (show || show_prop) {
		/* show mode */
		if (repo_mode)
			rv = repo_show_pkg_info(&xh, argv[optind], props);
		else
			rv = show_pkg_info_from_metadir(&xh,
			    argv[optind], props);

	} else if (show_files) {
		/* show-files mode */
		if (repo_mode)
			rv =  repo_show_pkg_files(&xh, argv[optind]);
		else
			rv = show_pkg_files_from_metadir(&xh, argv[optind]);

	} else if (show_deps) {
		/* show-deps mode */
		if (show_deps > 1)
			fulldeptree = true;

		if (repo_mode)
			rv = repo_show_pkg_deps(&xh, argv[optind], fulldeptree);
		else
			rv = show_pkg_deps(&xh, argv[optind], fulldeptree);

	} else if (show_rdeps) {
		/* show-rdeps mode */
		if (repo_mode)
			rv = repo_show_pkg_revdeps(&xh, argv[optind]);
		else
			rv = show_pkg_revdeps(&xh, argv[optind]);
	}

	exit(rv);
}
