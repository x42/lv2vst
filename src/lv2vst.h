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

#ifndef __lv2vst__
#define __lv2vst__

#include <math.h>
#include <pthread.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/uri-map/uri-map.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/instance-access/instance-access.h"

#include "lv2desc.h"
#include "ringbuffer.h"
#include "uri_map.h"
#include "vst.h"
#include "worker.h"

struct URIs {
	LV2_URID midi_MidiEvent;
	LV2_URID atom_Sequence;
	LV2_URID atom_EventTransfer;

	LV2_URID time_Position;
	LV2_URID time_frame;
	LV2_URID time_speed;
	LV2_URID time_bar;
	LV2_URID time_barBeat;
	LV2_URID time_beatUnit;
	LV2_URID time_beatsPerBar;
	LV2_URID time_beatsPerMinute;

	LV2_URID atom_Float;
	LV2_URID atom_Int;

	LV2_URID param_sampleRate;
	LV2_URID bufsz_minBlockLength;
	LV2_URID bufsz_maxBlockLength;
	LV2_URID bufsz_sequenceSize;
};

class LV2Vst;

class Lv2VstUI : public VstGui
{
	public:
		Lv2VstUI (LV2Vst* effect);
		virtual ~Lv2VstUI ();

		virtual bool get_rect (ERect** rect);
		virtual bool open (void* ptr, float scale_factor);
		virtual void close ();
		virtual bool is_open () const;
		virtual void idle ();

		bool has_editor () const { return plugin_gui != NULL; }

		void write_to_dsp (uint32_t port_index, uint32_t buffer_size, uint32_t port_protocol, const void* buffer);

		static int ui_resize (LV2UI_Feature_Handle handle, int width, int height) {
			Lv2VstUI* self = (Lv2VstUI*) handle;
			self->set_size (width, height);
			return 0;
		}

		void set_size (int width, int height);

	protected:
		LV2Vst * _lv2vst;

		const LV2UI_Descriptor *plugin_gui;
		LV2UI_Handle gui_instance;

		LV2UI_Widget _widget;
		ERect _rect;

		LV2_URID_Map   uri_map;
		LV2_URID_Unmap uri_unmap;
		LV2UI_Resize   lv2ui_resize;

		LV2UI_Idle_Interface* _idle_iface;
		LV2_Atom_Sequence* _atombuf;
		LV2_URID _uri_atom_EventTransfer;
		LV2_URID _uri_atom_Float;

		void* _lib_handle;
		uint32_t _port_event_recursion;

		// used for UI options
		float _sample_rate;
		float _scale_factor;
};

class LV2Vst : public VstPlugin
{
	public:
		LV2Vst (audioMasterCallback, RtkLv2Description*);
		~LV2Vst ();

		// Processing
		virtual void process (float**, float**, int32_t);

		// Parameters
		virtual float get_parameter (int32_t index);
		virtual bool set_parameter (int32_t index, float value);
		virtual void get_parameter_label (int32_t index, char* label);
		virtual void get_parameter_display (int32_t index, char* text);
		virtual void get_parameter_name (int32_t index, char* text);
		virtual bool get_parameter_properties (int32_t index, VstParameterProperties* p);
		virtual bool can_be_automated (int32_t index);

		virtual bool get_effect_name (char* name);
		virtual bool get_vendor_string (char* text);
		virtual bool get_product_string (char* text);
		virtual int32_t get_vendor_version ();

		virtual void set_sample_rate (float sr);
		virtual void set_block_size (int32_t bs);
		virtual void resume ();
		virtual void suspend ();

		virtual int32_t get_chunk (void** data, bool is_preset);
		virtual int32_t set_chunk (void* data, int32_t size, bool is_preset);

		virtual int32_t can_do (char* text);
		virtual int32_t bypass_plugin (bool bypass);

		virtual VstPlugCategory get_category ();
		virtual float get_sample_rate ();

		virtual int32_t process_events (VstEvents* events);

		LV2_Handle plugin_instance () const { return _plugin_instance; }
		LV2_Descriptor const* plugin_dsp () const { return _plugin_dsp; }
		RtkLv2Description const* desc () const { return _desc; }
		const char* bundle_path () const { return _desc->bundle_path; }

		uint32_t portmap_atom_to_ui () const { return _portmap_atom_to_ui; }
		uint32_t portmap_ctrl (uint32_t i) const { return _portmap_ctrl[i]; }

		struct ParamVal {
			ParamVal () : p (0) , v (0) {}
			ParamVal (uint32_t pp, float vv) : p (pp), v (vv) {}
			uint32_t p;
			float    v;
		};

		float param_to_vst (uint32_t index, float) const;
		float param_to_lv2 (uint32_t index, float) const;

		Lv2VstUtil::RingBuffer<struct ParamVal> ctrl_to_ui;
		Lv2VstUtil::RingBuffer<char> atom_to_ui;
		Lv2VstUtil::RingBuffer<char> atom_from_ui;

		void* map_instance () const { return (void*)&_map; }
		LV2_URID map_uri (const char* uri) {
			return _map.uri_to_id (uri);
		}

		struct LV2PortProperty {
			uint32_t key;
			uint32_t type;
			uint32_t flags;
			uint32_t size;
			void*    value;
		};

		struct LV2PortValue {
			float    value;
			char*    symbol;
		};

		struct LV2State {
			uint32_t         n_props;
			uint32_t         n_values;
			LV2PortProperty* props;
			LV2PortValue*    values;
		};

	protected:
		void init ();
		void deinit ();

		size_t serialize_state (LV2State* state, void** data);
		LV2State* unserialize_state (void* data, size_t s);

		const LV2Port* index_to_desc (int32_t) const;

		RtkLv2Description*     _desc;
		const LV2_Descriptor*  _plugin_dsp;
		LV2_Handle             _plugin_instance;

		Lv2UriMap  _map;
		Lv2VstUI   _ui;
		Lv2Worker* _worker;
		URIs       _uri;

		LV2_Atom_Forge        lv2_forge;
		LV2_Worker_Schedule   schedule;
		LV2_URID_Map          uri_map;
		LV2_URID_Unmap        uri_unmap;

		const LV2_Worker_Interface* worker_iface;
		const LV2_Options_Interface* opts_iface;

		uint32_t* _portmap_ctrl;
		uint32_t* _portmap_rctrl;

		LV2_Atom_Sequence* _atom_in;
		LV2_Atom_Sequence* _atom_out;
		uint32_t _portmap_atom_to_ui;
		uint32_t _portmap_atom_from_ui;

		float* _ports;
		float* _ports_pre;

		Lv2VstUtil::RingBuffer<VstMidiEvent> midi_buffer;

		bool _ui_sync;
		bool _active;
		VstTimeInfo _ti;

		void* _lib_handle;
		const LV2_Descriptor* (*lv2_descriptor)(uint32_t index);
};
#endif
