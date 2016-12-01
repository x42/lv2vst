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
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
# include <winsock2.h>
# include <windows.h>
#else
# include <arpa/inet.h>
#endif

#include "lv2vst.h"

static void free_lv2state (LV2Vst::LV2State* state)
{
	for (uint32_t i = 0; i < state->n_props; ++i) {
		free (state->props[i].value);
	}
	for (uint32_t i = 0; i < state->n_values; ++i) {
		free (state->values[i].symbol);
	}
	free (state);
}

static size_t serialize_string (uint8_t *d, const char* str)
{
	uint32_t len = strlen (str);
	uint32_t v = htonl (len);
	memcpy (d, &v, sizeof (uint32_t));
	d += sizeof (uint32_t);
	memcpy (d, str, len);
	return len + sizeof (uint32_t);
}

static size_t unserialize_string (uint8_t *d, char** str)
{
	uint32_t v;
	memcpy (&v, d, sizeof (uint32_t));
	d += sizeof (uint32_t);
	uint32_t len = ntohl (v);
	*str = (char*) malloc (len + 1);
	memcpy (*str, d, len);
	(*str)[len] = 0;
	return len + sizeof (uint32_t);
}

// TODO use .ttl instead (save LV2 presets) ??
size_t LV2Vst::serialize_state (LV2State* state, void** data)
{
	size_t size = 2 * sizeof (uint32_t);

	for (uint32_t i = 0; i < state->n_props; ++i) {
		LV2PortProperty *p = &state->props[i];
		size += sizeof (uint32_t) * 4 + state->props[i].size;
		size += strlen (_map.id_to_uri (p->key));
		size += strlen (_map.id_to_uri (p->type));
	}
	for (uint32_t i = 0; i < state->n_values; ++i) {
		size += sizeof (float);
		size += sizeof (uint32_t);
		size += strlen (state->values[i].symbol);
	}

	uint8_t* d = (uint8_t*) malloc (size);
	uint32_t v;
	*data = (void*) d;

	v = htonl (state->n_props);  memcpy (d, &v, sizeof (uint32_t)); d += sizeof (uint32_t);
	v = htonl (state->n_values); memcpy (d, &v, sizeof (uint32_t)); d += sizeof (uint32_t);

	for (uint32_t i = 0; i < state->n_props; ++i) {
		LV2PortProperty *p = &state->props[i];
		d += serialize_string (d, _map.id_to_uri (p->key));
		d += serialize_string (d, _map.id_to_uri (p->type));
		v = htonl (p->flags); memcpy (d, &v, sizeof (uint32_t)); d += sizeof (uint32_t);
		v = htonl (p->size);  memcpy (d, &v, sizeof (uint32_t)); d += sizeof (uint32_t);
		memcpy (d, p->value, p->size); d += p->size;
	}
	for (uint32_t i = 0; i < state->n_values; ++i) {
		LV2PortValue *p = &state->values[i];
		memcpy (d, &p->value, sizeof (float)); d += sizeof (float); // portable?
		d += serialize_string (d, p->symbol);
	}
	return size;
}

// TODO use .ttl instead (read presets) ??
LV2Vst::LV2State* LV2Vst::unserialize_state (void* data, size_t s)
{
	if (s < 2 * sizeof (uint32_t)) {
		return NULL;
	}

#define GETUINT(T)                     \
  {                                    \
    uint32_t v;                        \
    memcpy (&v, d, sizeof (uint32_t)); \
    T = ntohl (v);                     \
    d += sizeof (uint32_t);            \
  }

	uint8_t* d = (uint8_t*) data;
	LV2State* const state = (LV2State*)calloc (1, sizeof (LV2State));
	GETUINT (state->n_props);
	GETUINT (state->n_values);

	state->props  = (LV2PortProperty*) calloc (state->n_props, sizeof (LV2PortProperty));
	state->values = (LV2PortValue*) calloc (state->n_values, sizeof (LV2PortValue));

	for (uint32_t i = 0; i < state->n_props; ++i) {
		LV2PortProperty *p = &state->props[i];
		char *k = NULL;
		d += unserialize_string (d, &k);
		p->key = _map.uri_to_id (k);
		free (k);

		k = NULL;
		d += unserialize_string (d, &k);
		p->type = _map.uri_to_id (k);
		free (k);

		GETUINT (p->flags);
		GETUINT (p->size);
		p->value = malloc (p->size);
		memcpy (p->value, d, p->size); d += p->size;
	}
	for (uint32_t i = 0; i < state->n_values; ++i) {
		LV2PortValue *p = &state->values[i];
		memcpy (&p->value, d, sizeof (float)); d += sizeof (float); // portable?
		d += unserialize_string (d, &p->symbol);
	}
	return state;
}

static LV2_State_Status store_callback (
		LV2_State_Handle handle,
		uint32_t         key,
		const void*      value,
		size_t           size,
		uint32_t         type,
		uint32_t         flags)
{
	LV2Vst::LV2State* const state = (LV2Vst::LV2State*)handle;
	state->props = (LV2Vst::LV2PortProperty*) realloc (state->props, (state->n_props + 1) * sizeof (LV2Vst::LV2PortProperty));
	LV2Vst::LV2PortProperty* const prop = &state->props[state->n_props];
	++state->n_props;

	if (flags & LV2_STATE_IS_POD) {
		prop->value = malloc (size);
		memcpy (prop->value, value, size);
	} else {
		prop->value = (void*)value;
	}

	prop->size  = size;
	prop->key   = key;
	prop->type  = type;
	prop->flags = flags;

	return LV2_STATE_SUCCESS;
}

static const void*
retrieve_callback (LV2_State_Handle handle,
                  uint32_t         key,
                  size_t*          size,
                  uint32_t*        type,
                  uint32_t*        flags)
{
	LV2Vst::LV2State* const state = (LV2Vst::LV2State*)handle;
	LV2Vst::LV2PortProperty const* prop = NULL;
	for (uint32_t i = 0; i < state->n_props; ++i) {
		if (state->props[i].key == key) {
			prop = &state->props[i];
			break;
		}
	}

	if (prop) {
		*size  = prop->size;
		*type  = prop->type;
		*flags = prop->flags;
		return prop->value;
	}
	return NULL;
}

int32_t LV2Vst::get_chunk (void** data, bool /*is_preset*/)
{
	LV2State* const state = (LV2State*)calloc (1, sizeof (LV2State));

	for (uint32_t p = 0; p < _desc->nports_total; ++p) {
		if (_desc->ports[p].porttype != CONTROL_IN) {
			continue;
		}
		state->values = (LV2PortValue*) realloc (state->values, (state->n_values + 1) * sizeof (LV2PortValue));
		state->values[state->n_values].value = _ports[p];
		state->values[state->n_values].symbol = strdup (_desc->ports[p].symbol);
		++state->n_values;
	}

	const LV2_State_Interface* iface = NULL;

	if (_plugin_dsp->extension_data) {
		iface = (const LV2_State_Interface*)_plugin_dsp->extension_data (LV2_STATE__interface);
	}

	/* TODO provide features to the plugin
	 * e.g. save file -- then serialize file and include in blob
	 * in VST fashion.
	 */
	if (iface && iface->save) {
		LV2_State_Status st = iface->save (_plugin_instance, store_callback, state, 0, NULL);
		if (st != LV2_STATE_SUCCESS) {
			fprintf (stderr, "LV2Host: Error saving plugin state\n");
		}
	}

	size_t sz = serialize_state (state, data);
	free_lv2state (state);
	return sz;
}

int32_t LV2Vst::set_chunk (void* data, int32_t size, bool /*is_preset*/)
{
	LV2State* const state = unserialize_state (data, size);
	if (!state) {
		fprintf (stderr, "failed to de-serialize state\n");
		return 0;
	}

	for (uint32_t i = 0; i < state->n_values; ++i) {
		LV2PortValue *pv = &state->values[i];
		for (uint32_t p = 0; p < _desc->nports_total; ++p) {
			if (_desc->ports[p].porttype != CONTROL_IN) {
				continue;
			}
			if (strcmp (_desc->ports[p].symbol, pv->symbol)) {
				continue;
			}
			if (_ports[p] == pv->value) {
				continue;
			}

			_ports[p] = pv->value;
			if (_ui.is_open ()) {
				if (ctrl_to_ui.write_space () > 0) {
					ParamVal pv (p, _ports[p]);
					ctrl_to_ui.write (&pv, 1);
				}
			}
			set_parameter_automated (portmap_ctrl (p), param_to_vst(p, pv->value)); // Tell host about it
		}
	}

	const LV2_State_Interface* iface = NULL;
	if (_plugin_dsp->extension_data) {
		iface = (const LV2_State_Interface*)_plugin_dsp->extension_data (LV2_STATE__interface);
	}
	if (iface && iface->restore) {
		iface->restore (_plugin_instance, retrieve_callback, (LV2_State_Handle)state, 0, NULL);
	}

	free_lv2state (state);
	return 0;
}
