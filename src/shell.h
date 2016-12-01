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

#ifndef _vst_shell_h_
#define _vst_shell_h_

#include "vst.h"
#include "lilv/lilv.h"

class LV2ShellPlugin : public VstPlugin
{
	public:
		LV2ShellPlugin (audioMasterCallback audioMaster, char** bundles, char** wl, char** bl)
			: VstPlugin (audioMaster, 0)
			, world (lilv_world_new ())
			, _bundles (bundles)
			, _whitelist (wl)
			, _blacklist (bl)
		{

#ifndef NDEBUG
			dump_lines ("BNDL: ", _bundles);
			dump_lines ("WL: ", _whitelist);
			dump_lines ("BL: ", _blacklist);
#endif

			// code-dup src/lv2ttl.cc
			unsigned int bndl_it = 0;
			if (bundles) {
				while (bundles[bndl_it]) {

					char b_path[1024];
#ifdef PLATFORM_WINDOWS
					snprintf (b_path, 1023, "%s\\%s\\", get_lib_path (), bundles[bndl_it]);
#else
					snprintf (b_path, 1023, "%s/%s/", get_lib_path (), bundles[bndl_it]);
#endif
					b_path[1023] = 0;
					++bndl_it;

					LilvNode *node = lilv_new_file_uri (world, NULL, b_path);
					lilv_world_load_bundle (world, node);
					lilv_node_free(node);
				}
			}

			if (bndl_it == 0) {
				lilv_world_load_all (world);
			}
			// end code-dup

			all_plugins = lilv_world_get_all_plugins (world);
			iter = lilv_plugins_begin (all_plugins);
		}

		int32_t shell_get_next_plugin (char* name) {
			if (lilv_plugins_is_end (all_plugins, iter)) {
				return 0;
			}

			const LilvPlugin* p = lilv_plugins_get (all_plugins, iter);
			const char* uri = lilv_node_as_uri (lilv_plugin_get_uri (p));

			bool ok = false;
			if (!_whitelist || _whitelist[0] == NULL) {
				ok = true;
			} else {
				unsigned int list_it = 0;
				while (!ok && _whitelist[list_it]) {
					size_t len = strlen (_whitelist[list_it]);
					if (len > 0 && !strncmp (uri, _whitelist[list_it], len)) {
						ok = true;
					}
					++list_it;
				}
			}

			if (_blacklist) {
				unsigned int list_it = 0;
				while (ok && _blacklist[list_it]) {
					size_t len = strlen (_blacklist[list_it]);
					if (len > 0 && !strncmp (uri, _blacklist[list_it], len)) {
						ok = false;
					}
					++list_it;
				}
			}

			if (!ok) {
				iter = lilv_plugins_next (all_plugins, iter);
				return shell_get_next_plugin (name);
			}

			uint32_t id = uri_to_id (uri);

#if 1 // test required features
			RtkLv2Description* plugin = get_desc_by_id (id, _bundles);
			if (!plugin) {
				iter = lilv_plugins_next (all_plugins, iter);
				return shell_get_next_plugin (name);
			}

# if 0 // try instantiate -- don't return NULL for a sub-plugin later.

			/* bitwig-studio will ignore _ALL_ in the shell if only one fails.
			 * all hail Ardour & Reaper. */

			LV2Vst* test;
			try {
				test = new LV2Vst (audioMaster, plugin);
			} catch (...) {
				fprintf (stderr, "CRASH %08x\n", id);
				free_desc (plugin);
				iter = lilv_plugins_next (all_plugins, iter);
				return shell_get_next_plugin (name);
			}
			delete test;
# endif
			free_desc (plugin);
#endif

			LilvNode* n = lilv_plugin_get_name (p);
			strncpy (name, lilv_node_as_string (n), 64);
			name[64] = 0;
#ifndef NDEBUG
			printf ("ID %08x -- %s\n", id, name);
#endif
			lilv_node_free (n);
			iter = lilv_plugins_next (all_plugins, iter);
			return id;
		}

		~LV2ShellPlugin () {
			lilv_world_free (world);
			free_lines (_bundles);
			free_lines (_blacklist);
			free_lines (_whitelist);
		}

		VstPlugCategory get_category () {
			return kPlugCategShell;
		}

		void process (float**, float**, int32_t) {}

	private:
		LilvWorld* world;
		const LilvPlugins* all_plugins;
		LilvIter* iter;

		char** _bundles;
		char** _whitelist;
		char** _blacklist;
};
#endif
