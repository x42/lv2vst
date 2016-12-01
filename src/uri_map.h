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

#ifndef _uri_map_h
#define _uri_map_h

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"

class Lv2UriMap
{
	public:
		Lv2UriMap () : urimap (NULL) , urimap_len (0) {}
		~Lv2UriMap () { free_uri_map (); }

		static LV2_URID uri_to_id (LV2_URI_Map_Callback_Data callback_data, const char* uri) {
			Lv2UriMap* self = (Lv2UriMap*) callback_data;
			return self->uri_to_id (uri);
		}

		static const char* id_to_uri (LV2_URI_Map_Callback_Data callback_data, LV2_URID urid) {
			Lv2UriMap* self = (Lv2UriMap*) callback_data;
			return self->id_to_uri (urid);
		}

		LV2_URID uri_to_id (const char* uri) {
			for (uint32_t i = 0; i < urimap_len; ++i) {
				if (!strcmp (urimap[i], uri)) {
					//printf("Found mapped URI '%s' -> %d\n", uri, i + 1);
					return i + 1;
				}
			}
			//printf("map URI '%s' -> %d\n", uri, urimap_len + 1);
			urimap = (char**) realloc (urimap, (urimap_len + 1) * sizeof (char*));
			urimap[urimap_len] = strdup (uri);
			return ++urimap_len;
		}

		const char* id_to_uri (LV2_URID i) {
			assert (i > 0 && i <= urimap_len);
			if (i == 0 || i > urimap_len) {
				fprintf (stderr, "LV2Host: invalid URID lookup\n");
				return NULL;
			}
			//printf("lookup URI %d -> '%s'\n", i, urimap[i - 1]);
			return urimap[i - 1];
		}

	private:
		void free_uri_map () {
			for (uint32_t i = 0; i < urimap_len; ++i) {
				free (urimap[i]);
			}
			free (urimap);
			urimap = NULL;
			urimap_len = 0;
		}

		char **urimap;
		uint32_t urimap_len;
};
#endif
