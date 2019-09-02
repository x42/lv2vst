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
#include <string.h>

#ifdef _WIN32
# include <windows.h>
# include <direct.h>
# define dlopen(path, flags) LoadLibrary(path)
# define dlclose(lib)        FreeLibrary((HMODULE)lib)
static inline const char* dlerror(void) { return "Unknown error"; }
#else
# include <dlfcn.h>
# include <unistd.h>
#endif

#include "loadlib.h"

void* open_lv2_lib (const char* lib_path, bool persist)
{
	dlerror();

#ifndef _WIN32
	int flags = RTLD_NOW;
#endif

#if ! (defined BUNDLES || defined _WIN32 || defined __APPLE__)
	/* Linux, likely with system-wide, dynamically linked plugins.
	 *
	 * In many such cases unloading UI libs can cause issues
	 * (e.g. re-initialize pango font-cache, re-use gobject, because
	 * static variables are initialized when the object is reloaded).
	 * To allows to work around this issue, by not unloading the module.
	 */
	if (persist) {
		flags |= RTLD_NODELETE;
	}
#endif

	void* lib = dlopen (lib_path, flags);
	if (!lib) {
		fprintf (stderr, "LV2Host: Failed to open library %s (%s)\n", lib_path, dlerror());
		return NULL;
	}
	return lib;
}

void close_lv2_lib (void* handle)
{
	if (!handle) {
		return;
	}
	dlclose (handle);
}

VstVoidFunc x_dlfunc (void* handle, const char* symbol)
{
	if (!handle) {
		return NULL;
	}
#ifdef _WIN32
	return (VstVoidFunc)GetProcAddress ((HMODULE)handle, symbol);
#else
	typedef VstVoidFunc (*VoidFuncGetter)(void*, const char*);
	VoidFuncGetter dlfunc = (VoidFuncGetter)dlsym;
	return dlfunc (handle, symbol);
#endif
}

static char libpath[1024] = "";

#ifdef WIN32
#include <windows.h>
BOOL WINAPI DllMain (HINSTANCE handle, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == 1) {
		GetModuleFileNameA (handle, libpath, 1024);
		char* sep = strrchr (libpath, '\\');
		if (sep) { *sep = 0; }
	}
	return TRUE;
}
#else
#include <libgen.h>
#include <dlfcn.h>
__attribute__((constructor))
static void on_load(void) {
	Dl_info dl_info;
	dladdr ((void *)on_load, &dl_info);
	strncpy (libpath, dl_info.dli_fname, 1024);
	libpath[1023] = 0;
	char* sep = strrchr (libpath, '/');
	if (sep) { *sep = 0; }
}
#endif

const char* get_lib_path () {
	return libpath;
}
