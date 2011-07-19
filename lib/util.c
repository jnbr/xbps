/*-
 * Copyright (c) 2008-2011 Juan Romero Pardines.
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

#ifdef HAVE_VASPRINTF
# define _GNU_SOURCE	/* for vasprintf(3) */
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/utsname.h>

#include "config.h"
#include "xbps_api_impl.h"

/**
 * @file lib/util.c
 * @brief Utility routines
 * @defgroup util Utility functions
 */
bool
xbps_check_is_repository_uri_remote(const char *uri)
{
	assert(uri != NULL);

	if ((strncmp(uri, "https://", 8) == 0) ||
	    (strncmp(uri, "http://", 7) == 0) ||
	    (strncmp(uri, "ftp://", 6) == 0))
		return true;

	return false;
}

int
xbps_check_is_installed_pkg_by_pattern(const char *pattern)
{
	prop_dictionary_t dict;
	pkg_state_t state;

	assert(pattern != NULL);

	if ((dict = xbps_find_pkg_dict_installed(pattern, true)) == NULL)
		dict = xbps_find_virtualpkg_dict_installed(pattern, true);
	if (dict == NULL) {
		if (errno == ENOENT) {
			errno = 0;
			return 0; /* not installed */
		}	
		return -1; /* error */
	}
	/*
	 * Check that package state is fully installed, not
	 * unpacked or something else.
	 */
	if (xbps_pkg_state_dictionary(dict, &state) != 0) {
		prop_object_release(dict);
		return -1; /* error */
	}
	if (state != XBPS_PKG_STATE_INSTALLED) {
		prop_object_release(dict);
		return 0; /* not fully installed */
	}
	prop_object_release(dict);

	return 1;
}

bool
xbps_check_is_installed_pkg_by_name(const char *pkgname)
{
	prop_dictionary_t pkgd;

	assert(pkgname != NULL);

	pkgd = xbps_find_pkg_dict_installed(pkgname, false);
	if (pkgd == NULL)
		pkgd = xbps_find_virtualpkg_dict_installed(pkgname, false);

	if (pkgd) {
		prop_object_release(pkgd);
		return true;
	}

	return false;
}

const char *
xbps_pkg_version(const char *pkg)
{
	const char *tmp;

	assert(pkg != NULL);

	/* Get the required version */
	tmp = strrchr(pkg, '-');
	if (tmp == NULL)
		return NULL;

	return tmp + 1; /* skip first '-' */
}

const char *
xbps_pkg_revision(const char *pkg)
{
	const char *tmp;

	assert(pkg != NULL);

	/* Get the required revision */
	tmp = strrchr(pkg, '_');
	if (tmp == NULL)
		return NULL;

	return tmp + 1; /* skip first '_' */
}

char *
xbps_pkg_name(const char *pkg)
{
	const char *tmp;
	char *pkgname;
	size_t len = 0;

	assert(pkg != NULL);

	/* Get package name */
	tmp = strrchr(pkg, '-');
	if (tmp == NULL)
		return NULL;

	len = strlen(pkg) - strlen(tmp) + 1;
	pkgname = malloc(len);
	if (pkgname == NULL)
		return NULL;

	strlcpy(pkgname, pkg, len);

	return pkgname;
}

char *
xbps_pkgpattern_name(const char *pkg)
{
	char *res, *pkgname;
	size_t len;

	assert(pkg != NULL);

	res = strpbrk(pkg, "><=");
	if (res == NULL)
		return NULL;

	len = strlen(pkg) - strlen(res) + 1;
	pkgname = malloc(len);
	if (pkgname == NULL)
		return NULL;

	strlcpy(pkgname, pkg, len);

	return pkgname;
}

const char *
xbps_pkgpattern_version(const char *pkg)
{
	char *res;

	assert(pkg != NULL);

	res = strpbrk(pkg, "><=");
	if (res == NULL)
		return NULL;

	return res;
}

static char *
get_pkg_index_remote_plist(const char *uri)
{
	const struct xbps_handle *xhp;
	char *uri_fixed, *repodir;

	assert(uri != NULL);

	xhp = xbps_handle_get();
	uri_fixed = xbps_get_remote_repo_string(uri);
	if (uri_fixed == NULL)
		return NULL;

	repodir = xbps_xasprintf("%s/%s/%s/%s",
	    xhp->rootdir, XBPS_META_PATH, uri_fixed, XBPS_PKGINDEX);
	if (repodir == NULL) {
		free(uri_fixed);
		return NULL;
	}
	free(uri_fixed);

	return repodir;
}

char *
xbps_pkg_index_plist(const char *uri)
{
	struct utsname un;

	assert(uri != NULL);

	if (uname(&un) == -1)
		return NULL;

	if (xbps_check_is_repository_uri_remote(uri))
		return get_pkg_index_remote_plist(uri);

	return xbps_xasprintf("%s/%s/%s", uri, un.machine, XBPS_PKGINDEX);
}

char *
xbps_path_from_repository_uri(prop_dictionary_t pkg_repod, const char *repoloc)
{
	const struct xbps_handle *xhp;
	const char *filen, *arch;
	char *lbinpkg = NULL;

	assert(pkg_repod != NULL);
	assert(repoloc != NULL);

	if (!prop_dictionary_get_cstring_nocopy(pkg_repod,
	    "filename", &filen))
		return NULL;
	if (!prop_dictionary_get_cstring_nocopy(pkg_repod,
	    "architecture", &arch))
		return NULL;

	xhp = xbps_handle_get();
	/*
	 * First check if binpkg is available in cachedir.
	 */
	lbinpkg = xbps_xasprintf("%s/%s", xhp->cachedir, filen);
	if (lbinpkg == NULL)
		return NULL;

	if (access(lbinpkg, R_OK) == 0)
		return lbinpkg;

	free(lbinpkg);
	/*
	 * Local and remote repositories use the same path.
	 */
	return xbps_xasprintf("%s/%s/%s", repoloc, arch, filen);
}

bool
xbps_pkg_has_rundeps(prop_dictionary_t pkg)
{
	prop_array_t array;

	assert(pkg != NULL);

	array = prop_dictionary_get(pkg, "run_depends");
	if (array && prop_array_count(array) > 0)
		return true;

	return false;
}

#ifdef HAVE_VASPRINTF
char *
xbps_xasprintf(const char *fmt, ...)
{
	va_list ap;
	char *buf;

	va_start(ap, fmt);
	if (vasprintf(&buf, fmt, ap) == -1) {
		va_end(ap);
		return NULL;
	}
	va_end(ap);

	return buf;
}
#endif /* HAVE_VASPRINTF */
