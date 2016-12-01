/*
 *  Copyright (C) 2016 Robin Gareus <robin@gareus.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#ifdef _WIN32
# include <windows.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "loadlib.h"
#include "lv2ttl.h"
#include "lv2vst.h"


static void free_lines (char** ln) {
	unsigned int i = 0;
	if (!ln) {
		return;
	}
	while (ln[i]) {
		free (ln[i++]);
	}
	free (ln);
}

#ifndef NDEBUG
static void dump_lines (const char* prefix, char const * const* ln) {
	if (!ln || !ln[0]) {
		printf ("%s -- Empty --\n", prefix);
		return;
	}
	unsigned int list_it = 0;
	while (ln[list_it]) {
		printf ("%s'%s'\n", prefix, ln[list_it]);
		++list_it;
	}
}
#endif

#include "shell.h"

/* ****************************************************************************/

#ifdef BUNDLES

static const char* lv2bundles[] = {
#include BUNDLES
};

# ifdef WHITELIST
	static const char* lv2whitelist[] = {
# include WHITELIST
	};
# endif

#else
static char** load_file (const char* fn) {
	char** lines = NULL;
	char buf[1024];
	unsigned line = 0;
	unsigned c = 0;

	int fd = open (fn, 0);
	if (fd < 0) {
		return lines;
	}

	lines = (char**) calloc (1, sizeof (char*));
	while (1) { // portable bytewise i/o
		int eol = 0;
		char b;
		ssize_t rb = read (fd, &b, 1);

		if (rb == 1 && b != '\n') {
			assert (c < 1023);
			buf[c++] = b;
		}
		if (rb != 1 && c > 0) {
			eol = 1;
		}
		if (rb == 1 && b == '\n') {
			eol = 1;
		}
		if (c == 1023) {
			eol = 1;
		}

		if (eol) {
			if (c > 0) {
				buf[c] = 0;
				lines[line++] = strdup (buf);
				lines = (char**) realloc (lines, (line + 1) * sizeof (char*));
				lines[line] = NULL;
			}
			c = 0;
		}

		if (rb != 1) {
			break;
		}
	}
	close (fd);
	return lines;
}
#endif

/* ****************************************************************************
 * main instantiation method
 */

VstPlugin* instantiate_vst (audioMasterCallback audioMaster)
{
	if (!audioMaster) {
		return NULL;
	}

	int32_t id = 0;
	char** bundles = NULL;
	char** whitelist = NULL;
	char** blacklist = NULL;

#ifdef BUNDLES
	/* 0) hardcoded bundles */

	unsigned int bndl_it = 0;
	while (lv2bundles[bndl_it]) {
		bundles = (char**) realloc (bundles, (bndl_it + 2) * sizeof (char*));
		bundles[bndl_it] = strdup (lv2bundles[bndl_it]);
		bundles[++bndl_it] = NULL;
	}

	if (bndl_it == 0) {
			fprintf (stderr, "LV2Host: No bundles are defined\n");
			return NULL;
	}

	// additional whitelist
# ifdef WHITELIST
	unsigned int white_it = 0;
	while (lv2whitelist[white_it]) {
		whitelist = (char**) realloc (whitelist, (white_it + 2) * sizeof (char*));
		whitelist[white_it] = strdup (lv2whitelist[white_it]);
		whitelist[++white_it] = NULL;
	}
	if (bndl_it == 1 && white_it == 1) {
		// TODO: assure that it's a complete URI -- not a prefix (at compile time?!)
		// -> no plugin-shell, single plugin.
		id = uri_to_id (whitelist[0]);
	}
# else
#  error hardcoded bundle-list needs hardcoded URI whitelist
# endif

#else
	/* 1) If .bundle file exists in the same dir as the VST
	 *    -> only load bundle(s) specified in the file
	 *    (dirs relative to lv2vst.dll)
	 *
	 * *otherwise* use system-wide LV2 world
	 *
	 * 2) load .blacklist and .whitelist files if they exist
	 *    in the same dir as the VST (one URI per line)
	 *
	 *  If whitelist is a single URI -> no shell, single plugin
	 *  otherwise index all plugins, filter white + blacklist
	 */


#ifdef _WIN32
# define COMPOSE_FN "%s\\%s"
#else
# define COMPOSE_FN "%s/%s"
#endif

	char fn[1024];
	snprintf (fn, 1023, COMPOSE_FN, get_lib_path (), ".bundle");
	fn[1023] = 0;
	bundles = load_file (fn);

	snprintf (fn, 1023, COMPOSE_FN, get_lib_path (), ".whitelist");
	fn[1023] = 0;
	whitelist = load_file (fn);


	if (whitelist && whitelist[0] != NULL && whitelist[1] == NULL) {
		// TODO check if it's a plugin -- it could also be a prefix (multiple plugins)
		id = uri_to_id (whitelist[0]);
	}

#endif

	if (id == 0) {
		if ((int32_t)audioMaster (0, audioMasterCanDo, 0, 0, (void*)"shellCategory", 0) == 0){
			fprintf (stderr, "LV2Host: VST host does not support Shell plugins\n");
			return NULL;
		}

		id = (int32_t)audioMaster (0, audioMasterCurrentId, 0, 0, 0, 0);
	}

	if (id == 0) {
		/* plugin shell -- list all available */
#ifndef BUNDLES
		snprintf (fn, 1023, COMPOSE_FN, get_lib_path (), ".blacklist");
		fn[1023] = 0;
		blacklist = load_file (fn);
#endif
		// Note: LV2ShellPlugin free's bundles, wl & bl
		return new LV2ShellPlugin (audioMaster, bundles, whitelist, blacklist);
	}

	/* instantiate given plugin */

	RtkLv2Description* plugin = get_desc_by_id (id, bundles);
	free_lines (bundles);
	free_lines (whitelist);

	if (!plugin) {
		fprintf (stderr, "LV2Host: Failed to parse lv2 ttl.\n");
		return NULL;
	}

#ifndef NDEBUG
	printf ("VST-ID : %08x\n", plugin->id);
	printf ("DSP-LIB: %s\n", plugin->dsp_path);
	printf ("GUI-LIB: %s\n", plugin->gui_path);
	printf ("DSP-URI: %s\n", plugin->dsp_uri);
	printf ("GUI-URI: %s\n", plugin->gui_uri);
	printf ("GUI-BDL: %s\n", plugin->bundle_path);
#endif

	try {
		return new LV2Vst (audioMaster, plugin);
	} catch (...) {
		fprintf (stderr, "LV2Host: instantiation failed\n");
		free_desc (plugin);
	}
	return NULL;
}
