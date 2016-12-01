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

#ifndef _lv2ttl_h_
#define _lv2ttl_h_

#include "lv2desc.h"

RtkLv2Description* get_desc_by_id (uint32_t id, char const* const* bundle);
RtkLv2Description* get_desc_by_uri (const char* uri, char const* const* bundle);
void free_desc (RtkLv2Description* desc);
uint32_t uri_to_id (const char* plugin_uri);

#endif
