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

#ifndef _lv2desc_h_
#define _lv2desc_h_

#include <stdint.h>

#ifndef UINT32_MAX
# define UINT32_MAX (4294967295U)
#endif

enum PortType {
	CONTROL_IN = 0,
	CONTROL_OUT,
	AUDIO_IN,
	AUDIO_OUT,
	MIDI_IN,
	MIDI_OUT,
	ATOM_IN,
	ATOM_OUT
};

enum PluginCategory {
	LV2_Uncategorized,
	LV2_AnalyserPlugin,
	LV2_InstrumentPlugin,
	LV2_OscillatorPlugin,
	LV2_SpatialPlugin,
};

struct LV2Port {
	enum PortType porttype;

	char *name;
	char *symbol;
	char *doc;

	float val_default;
	float val_min;
	float val_max;
	float steps;

	bool toggled;
	bool integer_step;
	bool logarithmic;
	bool sr_dependent;
	bool enumeration;
	bool not_on_gui;
	bool not_automatic;
	//const char* unit; // or format ?
};

typedef struct _RtkLv2Description {
	char* dsp_uri;
	char* gui_uri;

	uint32_t id;

	char* plugin_name;
	char* vendor;
	char* bundle_path;
	char* dsp_path;
	char* gui_path;

	int version_minor;
	int version_micro;

	struct LV2Port *ports;

	uint32_t nports_total;
	uint32_t nports_audio_in;
	uint32_t nports_audio_out;
	uint32_t nports_midi_in;
	uint32_t nports_midi_out;
	uint32_t nports_atom_in;
	uint32_t nports_atom_out;
	uint32_t nports_ctrl;
	uint32_t nports_ctrl_in;
	uint32_t nports_ctrl_out;
	uint32_t min_atom_bufsiz;
	uint32_t latency_ctrl_port;
	uint32_t enable_ctrl_port;

	bool     send_time_info;
	bool     has_state_interface;

	PluginCategory category;
} RtkLv2Description;
#endif
