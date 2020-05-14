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

#ifndef _vst_plugin_
#define _vst_plugin_

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "vestige.h"

typedef struct {
	int16_t top;
	int16_t left;
	int16_t bottom;
	int16_t right;
} ERect;

class VstPlugin;

VstPlugin* instantiate_vst (audioMasterCallback audioMaster);

class VstGui
{
	public:
		VstGui (VstPlugin* effect = 0) : effect (effect) {}
		virtual ~VstGui () {}

		virtual bool get_rect (ERect** rect) = 0;
		virtual bool open (void* ptr, float scale_factor) = 0;
		virtual void close () = 0;
		virtual bool is_open () const = 0;
		virtual void idle () {}

	protected:
		VstPlugin* effect;
};

class VstPlugin
{
	public:
		VstPlugin (audioMasterCallback audioMaster, int32_t _n_params)
			: audioMaster (audioMaster)
			, _sample_rate (48000.f)
			, _block_size (8192)
			, _n_params (_n_params)
			, _editor (0)
			, _ui_scale_factor (1.0f)
		{
			memset (&_effect, 0, sizeof (AEffect));
			_effect.magic            = kEffectMagic;
			_effect.dispatcher       = _dispatcher_;
			_effect.setParameter     = _set_param_;
			_effect.getParameter     = _get_param_;
			_effect.processReplacing = _process_;
			_effect.numParams        = _n_params;
			_effect.object           = this;
			_effect.numPrograms      = 0;
		}

		virtual ~VstPlugin ()
		{
			if (_editor) {
				delete _editor;
			}
		}

		AEffect* get_effect () { return &_effect; }

		intptr_t dispatcher (int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
		{
			intptr_t v = 0;

			switch (opcode)
			{
				case effOpen:
					open ();
					break;
				case effClose:
					close ();
					break;
				case 6: // effGetParamLabel
					get_parameter_label (index, (char*)ptr);
					break;
				case 7: // effGetParamDisplay
					get_parameter_display (index, (char*)ptr);
					break;
				case effGetParamName:
					get_parameter_name (index, (char*)ptr);
					break;
				case effSetSampleRate:
					set_sample_rate (opt);
					break;
				case effSetBlockSize:
					set_block_size ((int32_t)value);
					break;
				case effMainsChanged:
					if (!value) { suspend (); } else { resume (); }
					break;
				case effEditGetRect:
					if (_editor) { v = _editor->get_rect ((ERect**)ptr) ? 1 : 0; }
					break;
				case effEditOpen:
					if (_editor) { v = _editor->open (ptr, _ui_scale_factor) ? 1 : 0; }
					break;
				case effEditClose:
					if (_editor) { _editor->close (); }
					break;
				case effEditIdle:
					if (_editor) { _editor->idle (); }
					break;
				case 23: // effGetChunk
					v = get_chunk ((void**)ptr, index ? true : false);
					break;
				case 24: // effSetChunk
					v = set_chunk (ptr, (int32_t)value, index ? true : false);
					break;
				case effProcessEvents:
					v = process_events ((VstEvents*)ptr);
					break;
				case 26: // effCanBeAutomated
					v = can_be_automated (index) ? 1 : 0;
					break;
				case effGetPlugCategory:
					v = (intptr_t)get_category ();
					break;
				case effGetEffectName:
					v = get_effect_name ((char*)ptr) ? 1 : 0;
					break;
				case effGetVendorString:
					v = get_vendor_string ((char*)ptr) ? 1 : 0;
					break;
				case effGetProductString:
					v = get_product_string ((char*)ptr) ? 1 : 0;
					break;
				case effGetVendorVersion:
					v = get_vendor_version ();
					break;
				case effCanDo:
					v = can_do ((char*)ptr);
					break;
				case 44 /* effSetBypass */:
					v = bypass_plugin (0 != value);
					break;
				case effGetParameterProperties:
					v = get_parameter_properties (index, (VstParameterProperties*)ptr) ? 1 : 0;
					break;
				case effShellGetNextPlugin:
					v = shell_get_next_plugin ((char*)ptr);
					break;
				case effGetVstVersion:
					v = 2400;
					break;
				case 50: // effVendorSpecific
					if (index == CCONST ('P', 'r', 'e', 'S') && value == CCONST ('A', 'e', 'C', 's')) {
						_ui_scale_factor = opt;
					}
					break;
				default:
#ifndef NDEBUG
					printf ("LV2Host: Unhandled opcode: %d\n", opcode);
#endif
					break;
			}
			return v;
		}

		virtual void process (float** inputs, float** outputs, int32_t n_samples) = 0;
		virtual int32_t process_events (VstEvents* events) { return 0; }

		virtual void open () {}
		virtual void close () {}
		virtual void suspend () {}
		virtual void resume () {}

		virtual float get_parameter (int32_t index) { return 0; }
		virtual bool set_parameter (int32_t index, float value) { return false; }
		virtual void set_parameter_automated (int32_t index, float value)
		{
			if (set_parameter (index, value)) {
				audioMaster (&_effect, audioMasterAutomate, index, 0, 0, value);
			}
		}

		virtual bool can_be_automated (int32_t index) { return true; }
		virtual bool get_parameter_properties (int32_t index, VstParameterProperties* p) { return false; }

		virtual void get_parameter_label (int32_t index, char* label)  { *label = 0; }
		virtual void get_parameter_display (int32_t index, char* text) { *text = 0; }
		virtual void get_parameter_name (int32_t index, char* text)    { *text = 0; }

		virtual int32_t get_chunk (void** data, bool is_preset = false) { return 0; }
		virtual int32_t set_chunk (void* data, int32_t size, bool is_preset = false) { return 0; }
		virtual void set_sample_rate (float sr)      { _sample_rate = sr; }
		virtual void set_block_size (int32_t bs)    { _block_size = bs; }

		virtual bool get_effect_name (char* name)    { return false; }
		virtual bool get_vendor_string (char* text)  { return false; }
		virtual bool get_product_string (char* text) { return false; }
		virtual int32_t get_vendor_version ()       { return 0; }
		virtual int32_t can_do (char* text)         { return 0; }
		virtual int32_t bypass_plugin (bool bypass) { return 0; }

		virtual VstPlugCategory get_category () = 0;
		virtual int32_t shell_get_next_plugin (char* name) { return 0; }

		virtual float update_sample_rate ()
		{
			intptr_t res = audioMaster (&_effect, audioMasterGetSampleRate, 0, 0, 0, 0);
			if (res > 0) {
				_sample_rate = (float)res;
			}
			return _sample_rate;
		}

		virtual int32_t update_block_size ()
		{
			intptr_t res = audioMaster (&_effect, audioMasterGetBlockSize, 0, 0, 0, 0);
			if (res > 0) {
				_block_size = (int32_t)res;
			}
			return _block_size;
		}

		virtual VstTimeInfo* get_time_info (int32_t filter)
		{
			intptr_t ret = audioMaster (&_effect, audioMasterGetTime, 0, filter, 0, 0);
			return (VstTimeInfo*) (ret);
		}

		bool send_events_to_host (VstEvents* events)
		{
			return audioMaster (&_effect, audioMasterProcessEvents, 0, 0, events, 0) == 1;
		}

		virtual bool io_changed ()
		{
			return (audioMaster (&_effect, audioMasterIOChanged, 0, 0, 0, 0) != 0);
		}
		virtual bool update_display ()
		{
			return (audioMaster (&_effect, audioMasterUpdateDisplay, 0, 0, 0, 0)) ? true : false;
		}

		virtual bool size_window (int32_t width, int32_t height)
		{
			return (audioMaster (&_effect, audioMasterSizeWindow, width, height, 0, 0) != 0);
		}

	protected:
		audioMasterCallback audioMaster;
		float    _sample_rate;
		int32_t  _block_size;
		int32_t  _n_params;

		VstGui*  _editor;
		AEffect  _effect;
		float    _ui_scale_factor;

	private:
		static intptr_t _dispatcher_ (AEffect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
		{
			VstPlugin* ae = (VstPlugin*)(e->object);
			if (opcode == effClose) {
				ae->dispatcher (opcode, index, value, ptr, opt);
				delete ae;
				return 1;
			}
			return ae->dispatcher (opcode, index, value, ptr, opt);
		}

		static float _get_param_ (AEffect* e, int32_t index) {
			VstPlugin* ae = (VstPlugin*)(e->object);
			return ae->get_parameter (index);
		}

		static void _set_param_ (AEffect* e, int32_t index, float value) {
			VstPlugin* ae = (VstPlugin*)(e->object);
			ae->set_parameter (index, value);
		}

		static void _process_ (AEffect* e, float** inputs, float** outputs, int32_t n_samples)
		{
			VstPlugin* ae = (VstPlugin*)(e->object);
			ae->process (inputs, outputs, n_samples);
		}
};

#endif
