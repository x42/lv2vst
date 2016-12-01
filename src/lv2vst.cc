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

#ifndef UPDATE_FREQ_RATIO
# define UPDATE_FREQ_RATIO 60 // MAX # of audio-cycles per GUI-refresh
#endif

static const size_t midi_buf_size = 8192;

#include "lv2/lv2plug.in/ns/ext/buf-size/buf-size.h"
#include "lv2/lv2plug.in/ns/ext/parameters/parameters.h"

#include "loadlib.h"
#include "lv2ttl.h"
#include "lv2vst.h"

extern "C" {
	char* lilv_dirname(const char* path);
}

/* ****************************************************************************
 * null terminated strncpy -- in VST fashion: sizeof (*dest) == n + 1;
 */

static inline void strncpyn (char* dest, const char* src, size_t n)
{
	strncpy (dest, src, n);
	dest[n] = 0;
}

/* ****************************************************************************
 * LV2 to VST Bridge Implementation
 */

LV2Vst::LV2Vst (audioMasterCallback audioMaster, RtkLv2Description* desc)
	: VstPlugin (audioMaster, desc->nports_ctrl_in)
	, ctrl_to_ui (1 + UPDATE_FREQ_RATIO * desc->nports_ctrl)
	, atom_to_ui (1 + UPDATE_FREQ_RATIO * desc->min_atom_bufsiz)
	, atom_from_ui (UPDATE_FREQ_RATIO * desc->min_atom_bufsiz)
	, _desc (desc)
	, _plugin_dsp (0)
	, _plugin_instance (0)
	, _ui (this)
	, _worker (0)
	, worker_iface (0)
	, opts_iface (0)
	, _portmap_atom_to_ui (UINT32_MAX)
	, _portmap_atom_from_ui (UINT32_MAX)
	, midi_buffer (midi_buf_size)
	, _ui_sync (true)
	, _active (false)
{
	_effect.numInputs = _desc->nports_audio_in;
	_effect.numOutputs = _desc->nports_audio_out;
	_effect.uniqueID = desc->id;
	_effect.flags |= effFlagsCanReplacing;
	_effect.version = 100 * _desc->version_minor + _desc->version_micro;

	if (_desc->has_state_interface) {
		_effect.flags |= (1 << 5);  // effFlagsProgramChunks;
	}

	_lib_handle = open_lv2_lib (desc->dsp_path);
	lv2_descriptor = (const LV2_Descriptor* (*)(uint32_t)) x_dlfunc (_lib_handle, "lv2_descriptor");

	if (!lv2_descriptor) {
		fprintf (stderr, "LV2Host: missing lv2_descriptor symbol for '%s'.\n", _desc->dsp_uri);
		throw -1;
	}

	memset (&_ti, 0, sizeof (VstTimeInfo));

	_ports = (float*) malloc (_desc->nports_total * sizeof (float));
	_ports_pre = (float*) malloc (_desc->nports_total * sizeof (float));
	_portmap_ctrl  = (uint32_t*) malloc (_desc->nports_total * sizeof (uint32_t));
	_portmap_rctrl = (uint32_t*) malloc (_desc->nports_ctrl_in * sizeof (uint32_t));

	_atom_in = (LV2_Atom_Sequence*) malloc (_desc->min_atom_bufsiz + sizeof (uint8_t));
	_atom_out = (LV2_Atom_Sequence*) malloc (_desc->min_atom_bufsiz + sizeof (uint8_t));

	/* prepare LV2 feature set */

	schedule.handle = NULL;
	schedule.schedule_work = &Lv2Worker::lv2_worker_schedule;
	uri_map.handle = &_map;
	uri_map.map = &Lv2UriMap::uri_to_id;
	uri_unmap.handle = &_map;
	uri_unmap.unmap = &Lv2UriMap::id_to_uri;

	init ();

	if (_ui.has_editor ()) {
		_editor = &_ui;
		_effect.flags |= effFlagsHasEditor;
	}
}

void LV2Vst::init ()
{

	_uri.atom_Float           = _map.uri_to_id (LV2_ATOM__Float);
	_uri.atom_Int             = _map.uri_to_id (LV2_ATOM__Int);
	_uri.param_sampleRate     = _map.uri_to_id (LV2_PARAMETERS__sampleRate);
	_uri.bufsz_minBlockLength = _map.uri_to_id (LV2_BUF_SIZE__minBlockLength);
	_uri.bufsz_maxBlockLength = _map.uri_to_id (LV2_BUF_SIZE__maxBlockLength);
	_uri.bufsz_sequenceSize   = _map.uri_to_id (LV2_BUF_SIZE__sequenceSize);

	/* options to pass to plugin */
	const LV2_Options_Option options[] = {
		{ LV2_OPTIONS_INSTANCE, 0, _uri.param_sampleRate,
			sizeof(float), _uri.atom_Float, &_sample_rate },
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_minBlockLength,
			sizeof(int32_t), _uri.atom_Int, &_block_size },
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_maxBlockLength,
			sizeof(int32_t), _uri.atom_Int, &_block_size },
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_sequenceSize,
			sizeof(int32_t), _uri.atom_Int, &midi_buf_size },
		{ LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL }
	};

	/* lv2 host features */
	const LV2_Feature schedule_feature = { LV2_WORKER__schedule, &schedule };
	const LV2_Feature map_feature      = { LV2_URID__map, &uri_map};
	const LV2_Feature unmap_feature    = { LV2_URID__unmap, &uri_unmap };
	const LV2_Feature options_feature  = { LV2_OPTIONS__options, (void*)&options };

	const LV2_Feature* features[] = {
		&map_feature,
		&unmap_feature,
		&schedule_feature,
		&options_feature,
		NULL
	};

	/* resolve descriptors */
	uint32_t index = 0;
	while (lv2_descriptor) {
		_plugin_dsp = lv2_descriptor (index);
		if (!_plugin_dsp) { break; }
		if (!strcmp (_plugin_dsp->URI, _desc->dsp_uri)) { break; }
		++index;
	}

	if (!_plugin_dsp) {
		fprintf (stderr, "LV2Host: cannot descriptor for '%s'.\n", _desc->dsp_uri);
		throw -2;
	}
	/* init plugin */
	update_block_size ();
	char* dirname = lilv_dirname (_desc->dsp_path);
	_plugin_instance = _plugin_dsp->instantiate (_plugin_dsp, update_sample_rate (), dirname, features);
	free (dirname);

	if (!_plugin_instance) {
		fprintf (stderr, "LV2Host: failed to instantiate '%s'.\n", _desc->dsp_uri);
		throw -3;
	}

	/* connect ports */
	uint32_t c_ctrl = 0;
	uint32_t c_ain  = 0;
	uint32_t c_aout = 0;

	for (uint32_t p=0; p < _desc->nports_total; ++p) {
		switch (_desc->ports[p].porttype) {
			case CONTROL_IN:
				_ports[p] = _desc->ports[p].val_default;
				_plugin_dsp->connect_port (_plugin_instance, p, &_ports[p]);
				{
					ParamVal pv (p, _ports[p]);
					ctrl_to_ui.write (&pv, 1);
				}
				if (!_desc->ports[p].not_on_gui && !_desc->ports[p].not_automatic) {
					_portmap_ctrl[p] = c_ctrl;
					_portmap_rctrl[c_ctrl] = p;
					c_ctrl++;
				} else {
					_portmap_ctrl[p] = UINT32_MAX;
				}
				break;
			case CONTROL_OUT:
				_plugin_dsp->connect_port (_plugin_instance, p, &_ports[p]);
				break;
			case MIDI_IN:
			case ATOM_IN:
				_portmap_atom_from_ui = p;
				_plugin_dsp->connect_port (_plugin_instance, p, _atom_in);
				break;
			case MIDI_OUT:
			case ATOM_OUT:
				_portmap_atom_to_ui = p;
				_plugin_dsp->connect_port (_plugin_instance, p, _atom_out);
				break;
			case AUDIO_IN:
				++c_ain;
				break;
			case AUDIO_OUT:
				++c_aout;
				break;
			default:
				break;
		}
	}

	_effect.numParams = c_ctrl;
	assert (c_ain == _desc->nports_audio_in);
	assert (c_aout == _desc->nports_audio_out);

	if (_desc->nports_atom_out > 0 || _desc->nports_atom_in > 0 || _desc->nports_midi_in > 0 || _desc->nports_midi_out > 0) {
		_uri.atom_Sequence       = _map.uri_to_id (LV2_ATOM__Sequence);
		_uri.atom_EventTransfer  = _map.uri_to_id (LV2_ATOM__eventTransfer);
		_uri.midi_MidiEvent      = _map.uri_to_id (LV2_MIDI__MidiEvent);
		_uri.time_Position       = _map.uri_to_id (LV2_TIME__Position);
		_uri.time_frame          = _map.uri_to_id (LV2_TIME__frame);
		_uri.time_speed          = _map.uri_to_id (LV2_TIME__speed);
		_uri.time_bar            = _map.uri_to_id (LV2_TIME__bar);
		_uri.time_barBeat        = _map.uri_to_id (LV2_TIME__barBeat);
		_uri.time_beatUnit       = _map.uri_to_id (LV2_TIME__beatUnit);
		_uri.time_beatsPerBar    = _map.uri_to_id (LV2_TIME__beatsPerBar);
		_uri.time_beatsPerMinute = _map.uri_to_id (LV2_TIME__beatsPerMinute);

		lv2_atom_forge_init (&lv2_forge, &uri_map);
	} else {
		memset (&_uri, 0, sizeof (URIs));
	}

	if (_plugin_dsp->extension_data) {
		worker_iface = (const LV2_Worker_Interface*) _plugin_dsp->extension_data (LV2_WORKER__interface);
		opts_iface = (const LV2_Options_Interface*) _plugin_dsp->extension_data (LV2_OPTIONS__interface);
	}

	if (worker_iface) {
		_worker = new Lv2Worker (worker_iface, _plugin_instance);
		schedule.handle = _worker;
	}
}

void LV2Vst::deinit ()
{
	delete _worker;

	suspend ();

	if (_plugin_dsp && _plugin_instance && _plugin_dsp->cleanup) {
		_plugin_dsp->cleanup (_plugin_instance);
	}
}

LV2Vst::~LV2Vst ()
{
	_editor = 0; // prevent delete in ~VstPlugin

	deinit ();

	free (_portmap_ctrl);
	free (_portmap_rctrl);
	free (_ports);
	free (_ports_pre);
	free (_atom_in);
	free (_atom_out);
	free_desc (_desc);
	close_lv2_lib (_lib_handle);
}

int32_t LV2Vst::can_do (char* text)
{
	if (!strcmp ("receiveVstEvents", text)) {
		return (_desc->nports_midi_in > 0) ? 1 : 0;
	}
	if (!strcmp ("receiveVstMidiEvent", text)) {
		return (_desc->nports_midi_in > 0) ? 1 : 0;
	}
	if (!strcmp ("sendVstEvents", text)) {
		return (_desc->nports_midi_out > 0) ? 1 : 0;
	}
	if (!strcmp ("sendVstMidiEvent", text)) {
		return (_desc->nports_midi_out > 0) ? 1 : 0;
	}
	if (!strcmp ("receiveVstTimeInfo", text)) {
		return _desc->send_time_info ? 1 : 0;
	}
	if (!strcmp ("bypass", text)) {
		return _desc->enable_ctrl_port != UINT32_MAX ? 1 : 0;
	}
#ifdef __APPLE__
	if (!strcmp ("hasCockosViewAsConfig", text)) {
		return 0xbeef0000;
	}
#endif
	return 0;
}

bool LV2Vst::get_effect_name (char* name)
{
	strncpyn (name, _desc->plugin_name, 32);
	return true;
}

bool LV2Vst::get_product_string (char* text)
{
	strncpyn (text, _desc->plugin_name, 64);
	return true;
}

bool LV2Vst::get_vendor_string (char* text)
{
	strncpyn (text, _desc->vendor, 64);
	return true;
}

int32_t LV2Vst::get_vendor_version ()
{
	return 1000;
}

VstPlugCategory LV2Vst::get_category ()
{
	// TODO -- map LV2 categories
	if (_desc->nports_audio_in == 0 && _desc->nports_midi_in == 0) {
		return kPlugCategGenerator;
	}
	if (_desc->nports_audio_in == 0 && _desc->nports_midi_in > 0 && _desc->nports_audio_out > 0) {
		return kPlugCategSynth;
	}
	if (_desc->nports_audio_in > 0 && _desc->nports_audio_out > 0) {
		return kPlugCategEffect;
	}
	return kPlugCategUnknown;
}

/* state.cc implements
 * LV2Vst::get_chunk ();
 * LV2Vst::set_chunk ();
 */

/* ****************************************************************************
 * Parameters
 */
const LV2Port* LV2Vst::index_to_desc (int32_t i) const
{
	if (i < 0 || i >= (int) _desc->nports_ctrl_in) {
		return NULL;
	}
	const uint32_t p = _portmap_rctrl[i];
	if (p == UINT32_MAX) {
		return NULL;
	}
	return &_desc->ports[p];
}

float LV2Vst::param_to_vst (uint32_t p, float v) const
{
	const LV2Port* l = &_desc->ports[p];
	float rv;

	if (l->toggled) {
		return v > 0 ? 1.f : 0.f;
	}

	if (l->integer_step) {
		v = rintf (v);
	}

	if (l->toggled) {
		rv = v <= 0.f ? 0.f : 1.f;
	} else if (l->logarithmic) {
		if (v < l->val_min) { v = l->val_min; }
		if (v > l->val_max) { v = l->val_max; }
		rv = log (v / l->val_min) / log (l->val_max / l->val_min);
	} else {
		const float range = l->val_max - l->val_min;
		rv = (v - l->val_min) / range;
	}
	return rv;
}

float LV2Vst::param_to_lv2 (uint32_t p, float v) const
{
	const LV2Port* l = &_desc->ports[p];
	float rv;

	v = rintf (l->steps * v) / l->steps;

	if (l->toggled) {
		rv = v >= 0.5f ? 1.f : 0.f;
	} else if (l->logarithmic) {
		if (v < 0.f) v = 0.f;
		if (v > 1.f) v = 1.f;
		rv = l->val_min * pow(l->val_max / l->val_min, v);
	} else {
		const float range = l->val_max - l->val_min;
		rv = l->val_min + v * range;
	}
	if (l->integer_step) {
		rv = rintf (rv);
	}
	return rv;
}

bool LV2Vst::set_parameter (int32_t i, float v)
{
	if (!index_to_desc (i)) {
		return false;
	}
	uint32_t p = _portmap_rctrl[i];
	const float val = param_to_lv2 (p, v);

	if (_ports[p] == val) {
		return false;
	}

	_ports[p] = val;
	if (_ui.is_open ()) {
		if (ctrl_to_ui.write_space () > 0) {
			ParamVal pv (p, _ports[p]);
			ctrl_to_ui.write (&pv, 1);
		}
	}
	return true;
}

float LV2Vst::get_parameter (int32_t i)
{
	if (!index_to_desc (i)) {
		return 0;
	}
	uint32_t p = _portmap_rctrl[i];
	return param_to_vst (p, _ports[p]);
}

void LV2Vst::get_parameter_name (int32_t i, char* label)
{
	const LV2Port* l = index_to_desc (i);
	if (l) {
		strncpyn (label, l->name, 8);
	}
}

void LV2Vst::get_parameter_display (int32_t i, char* text)
{
	const LV2Port* l = index_to_desc (i);
	if (!l) {
		return;
	}

	float v =  _ports[_portmap_rctrl[i]];

	if (l->sr_dependent) {
		v *= _sample_rate;
	}
	// TODO format -- scalepoints, add unit?
	snprintf (text, 8, "%.2f", v);
}

void LV2Vst::get_parameter_label (int32_t i, char* label)
{
	const LV2Port* l = index_to_desc (i);
	if (l) {
		strncpyn (label, l->doc, 8);
	}
}

bool LV2Vst::get_parameter_properties (int32_t i, VstParameterProperties* p)
{
	const LV2Port* l = index_to_desc (i);
	if (!l) {
		return false;
	}

	p->flags = kVstParameterSupportsDisplayIndex;
	p->displayIndex = i;

	if (l->toggled) {
		p->flags |= kVstParameterIsSwitch;
	}

	if (l->integer_step) {
		p->flags |= kVstParameterUsesIntStep | kVstParameterUsesIntegerMinMax;
		p->stepInteger = 1;
		p->minInteger = l->val_min;
		p->maxInteger = l->val_max;
	}else {
		p->flags |= kVstParameterUsesFloatStep;
		p->flags |= kVstParameterCanRamp; // ??

		p->stepFloat = 1.f / l->steps;
		p->smallStepFloat = p->stepFloat / 2.f;
		p->largeStepFloat = p->stepFloat * 5.f;

		if (p->largeStepFloat > 1.f) {
			p->largeStepFloat = 1.f;
		}
	}

	strncpyn (p->label, l->doc, 8);
	strncpyn (p->shortLabel, l->name, 8);

	return true;
}

bool LV2Vst::can_be_automated (int32_t i)
{
	const LV2Port* l = index_to_desc (i);
	return l && !l->not_automatic;
}

int32_t LV2Vst::bypass_plugin (bool bypass)
{
	if (_desc->enable_ctrl_port == UINT32_MAX) {
		return 0;
	}
	uint32_t vst_idx = portmap_ctrl (_desc->enable_ctrl_port);
	set_parameter (vst_idx, bypass ? 0 : 1); // notify UI, set immediate
	set_parameter_automated (vst_idx, bypass ? 0 : 1);
	return 1;
}

/* ****************************************************************************
 * State
 */

void LV2Vst::resume ()
{
	if (_active) {
		return;
	}
	if (_plugin_dsp->activate) {
		_plugin_dsp->activate (_plugin_instance);
	}
	if (_desc->nports_midi_in) {
		audioMaster (&_effect, audioMasterWantMidi, 0, 0, 0, 0);
	}
	_active = true;
}

void LV2Vst::suspend ()
{
	if (!_active) {
		return;
	}
	if (_plugin_dsp->deactivate) {
		_plugin_dsp->deactivate (_plugin_instance);
	}
	_active = false;
}

void LV2Vst::set_sample_rate (float rate)
{
	if (_sample_rate != rate) {
		VstPlugin::set_sample_rate (rate);
		deinit ();
		init ();
	}
}

void LV2Vst::set_block_size (int32_t bs)
{
	if (_block_size != bs) {
		VstPlugin::set_block_size (bs);
		if (opts_iface) {
			LV2_Options_Option block_size_option = {
				LV2_OPTIONS_INSTANCE, 0,
				_map.uri_to_id ("http://lv2plug.in/ns/ext/buf-size#nominalBlockLength"),
				sizeof(int32_t), _uri.atom_Int, (void*)&_block_size
			};
			opts_iface->set (_plugin_instance, &block_size_option);
		}
	}
}

/* ****************************************************************************
 * Process Audio/Midi
 */

int32_t LV2Vst::process_events (VstEvents* events)
{
	for (int32_t i = 0; i < events->numEvents; ++i) {
		VstMidiEvent* mev = (VstMidiEvent*) events->events[i];
		if (mev->type != kVstMidiType) {
			continue;
		}
		if (midi_buffer.write_space () > 0) {
			midi_buffer.write (mev, 1);
		}
	}
	return 0;
}

void LV2Vst::process (float** inputs, float** outputs, int32_t n_samples)
{
	int ins = 0;
	int outs = 0;

	/* re-connect audio buffers */
	for (uint32_t p = 0; p < _desc->nports_total; ++p) {
		switch (_desc->ports[p].porttype) {
			case AUDIO_IN:
				// check isInputConnected() in resume()
				//memset (outputs[ins], 0, n_samples * sizeof (float)); // if not connected
				_plugin_dsp->connect_port (_plugin_instance, p, inputs[ins++]);
				break;
			case AUDIO_OUT:
				// check isOutputConnected() in resume()
				//memset (outputs[outs], 0, n_samples * sizeof (float)); // if plugin is b0rked
				_plugin_dsp->connect_port (_plugin_instance, p, outputs[outs++]);
				break;
			default:
				break;
		}
	}

	/* Get transport position */
	VstTimeInfo *ti = get_time_info (kVstPpqPosValid | kVstBarsValid | kVstTimeSigValid | kVstTempoValid);

	const bool transport_changed = ti && (
			   ti->flags              != _ti.flags
			|| ti->samplePos          != _ti.samplePos
			|| ti->tempo              != _ti.tempo
			|| ti->timeSigDenominator != _ti.timeSigDenominator
			|| ti->timeSigNumerator   != _ti.timeSigNumerator
			);

	/* atom buffers */
	if (_desc->nports_atom_in > 0 || _desc->nports_midi_in > 0) {
		/* start Atom sequence */
		_atom_in->atom.type = _uri.atom_Sequence;
		_atom_in->atom.size = 8;
		LV2_Atom_Sequence_Body *body = &_atom_in->body;
		body->unit = 0; // URID of unit of event time stamp LV2_ATOM__timeUnit ??
		body->pad  = 0; // unused
		uint8_t* seq = (uint8_t*) (body + 1);

		if (transport_changed && _desc->send_time_info) {
			assert (ti);
			uint8_t   pos_buf[256];

			long int framepos = floor (ti->samplePos);
			bool     rolling = ti->flags & kVstTransportPlaying;

			LV2_Atom* lv2_pos = (LV2_Atom*)pos_buf;

			lv2_atom_forge_set_buffer (&lv2_forge, pos_buf, sizeof (pos_buf));
			LV2_Atom_Forge* forge = &lv2_forge;
			LV2_Atom_Forge_Frame frame;
			lv2_atom_forge_object (&lv2_forge, &frame, 1, _uri.time_Position);
			lv2_atom_forge_property_head (forge, _uri.time_frame, 0);
			lv2_atom_forge_long (forge, framepos);
			lv2_atom_forge_property_head (forge, _uri.time_speed, 0);
			lv2_atom_forge_float (forge, rolling ? 1.0 : 0.0);


			if ((ti->flags & (kVstPpqPosValid | kVstBarsValid)) == (kVstPpqPosValid | kVstBarsValid)) {
				const double ppq_scaling =  ti->timeSigDenominator / 4.0;
				float barbeat = (ti->ppqPos - ti->barStartPos) / ppq_scaling;
				long int bar = floor (ti->barStartPos / ti->timeSigNumerator / ppq_scaling);

				lv2_atom_forge_property_head (forge, _uri.time_barBeat, 0);
				lv2_atom_forge_property_head (forge, _uri.time_barBeat, 0);
				lv2_atom_forge_float (forge, barbeat);
				lv2_atom_forge_property_head (forge, _uri.time_bar, 0);
				lv2_atom_forge_long (forge, bar);

				lv2_atom_forge_property_head (forge, _uri.time_beatUnit, 0);
				lv2_atom_forge_int (forge, ti->timeSigDenominator);
				lv2_atom_forge_property_head (forge, _uri.time_beatsPerBar, 0);
				lv2_atom_forge_float (forge, ti->timeSigNumerator);
				lv2_atom_forge_property_head (forge, _uri.time_beatsPerMinute, 0);
				lv2_atom_forge_float (forge, ti->tempo);
			}

			uint32_t size = lv2_pos->size;
			uint32_t padded_size = ((sizeof (LV2_Atom_Event) + size) +  7) & (~7);

			if (_desc->min_atom_bufsiz > padded_size) {
				LV2_Atom_Event *aev = (LV2_Atom_Event *)seq;
				aev->time.frames = 0;
				aev->body.size   = size;
				aev->body.type   = lv2_pos->type;
				memcpy (LV2_ATOM_BODY (&aev->body), LV2_ATOM_BODY (lv2_pos), size);
				_atom_in->atom.size += padded_size;
				seq += padded_size;
			}
		}

		if (_ui.has_editor ()) {
			while (atom_from_ui.read_space () > sizeof (LV2_Atom)) {
				LV2_Atom a;
				atom_from_ui.read ((char *) &a, sizeof (LV2_Atom));
				uint32_t padded_size = _atom_in->atom.size + a.size + sizeof (int64_t);
				if (_desc->min_atom_bufsiz > padded_size) {
					memset (seq, 0, sizeof (int64_t)); // LV2_Atom_Event->time
					seq += sizeof (int64_t);
					atom_from_ui.read ((char *) seq, a.size);
					seq += a.size;
					_atom_in->atom.size += a.size + sizeof (int64_t);
				}
			}
		}

		if (_desc->nports_midi_in > 0) {
			/* inject midi events */
			while (midi_buffer.read_space () > 0) {
				VstMidiEvent mev;
				if (1 != midi_buffer.read (&mev, 1)) {
					continue;
				}

				uint32_t size = 3;
				uint8_t status = (mev.midiData[0]) & 0xf0;
				switch (status) {
					case 0xc0:  // program change
					case 0xd0:  // chan pressure
					case 0xf1:  // MTC QF
					case 0xf3:  // song select
						size = 2;
						break;
					case 0xf8: // MCLK tick
					case 0xfa: // MCLK start
					case 0xfb: // MCLK stop
					case 0xfe: // active sensing
					case 0xff: // reset
						size = 1;
						break;
					default:
						break;
				}


				uint32_t padded_size = ((sizeof (LV2_Atom_Event) + size) +  7) & (~7);

				if (_desc->min_atom_bufsiz > padded_size) {
					LV2_Atom_Event *aev = (LV2_Atom_Event *)seq;
					aev->time.frames = mev.deltaFrames;
					aev->body.size   = size;
					aev->body.type   = _uri.midi_MidiEvent;
					memcpy (LV2_ATOM_BODY (&aev->body), mev.midiData, size);
					_atom_in->atom.size += padded_size;
					seq += padded_size;
				}
			}
		}
	}

	if (_desc->nports_atom_out > 0 || _desc->nports_midi_out > 0) {
		_atom_out->atom.type = 0;
		_atom_out->atom.size = _desc->min_atom_bufsiz;
	}

	/* make a backup copy, to see what is changed */
	memcpy (_ports_pre, _ports, _desc->nports_total * sizeof (float));

	_plugin_dsp->run (_plugin_instance, n_samples);

	/* handle worker emit response  - may amend Atom seq... */
	if (_worker) {
		_worker->emit_response ();
	}

	/* bump expected time */
	if (ti) {
		memcpy (&_ti, ti, sizeof (VstTimeInfo));
		if (ti->flags & kVstTransportPlaying) {
			_ti.samplePos += n_samples;
		}
	}

	/* create port-events for changed values */

	if (_ui.is_open ()) {
		for (uint32_t p = 0; p < _desc->nports_total; ++p) {
			if (_desc->ports[p].porttype == CONTROL_IN && _ui_sync) {
				ParamVal pv (p, _ports[p]);
				ctrl_to_ui.write (&pv, 1);
				continue;
			}
			if (_desc->ports[p].porttype != CONTROL_OUT) {
				continue;
			}
			if (_ports_pre[p] == _ports[p] && !_ui_sync) {
				continue;
			}

			if (p == _desc->latency_ctrl_port) {
				_effect.initialDelay =  (floor (_ports_pre[p]));
				//io_changed ();
			}

			if (ctrl_to_ui.write_space () < 1) {
				continue;
			}
			ParamVal pv (p, _ports[p]);
			ctrl_to_ui.write (&pv, 1);
		}
		_ui_sync = false;
	} else {
		_ui_sync = true;
	}

	/* Atom sequence port-events */
	if (_desc->nports_atom_out + _desc->nports_midi_out > 0 && _atom_out->atom.size > sizeof (LV2_Atom)) {
		if (_ui.is_open () && atom_to_ui.write_space () >= _atom_out->atom.size + 2 * sizeof (LV2_Atom)) {
			LV2_Atom a = {_atom_out->atom.size + (uint32_t) sizeof (LV2_Atom), 0};

			atom_to_ui.write ((char *) &a, sizeof (LV2_Atom));
			atom_to_ui.write ((char *) _atom_out, a.size);
		}

		if (_desc->nports_midi_out) {
			LV2_Atom_Event const* ev = (LV2_Atom_Event const*)((&(_atom_out)->body) + 1); // lv2_atom_sequence_begin
			while ((const uint8_t*)ev < ((const uint8_t*) &(_atom_out)->body + (_atom_out)->atom.size)) {
				if (ev->body.type == _uri.midi_MidiEvent && ev->body.size < 4) {
					VstEvents vev;
					vev.numEvents = 1;
					VstMidiEvent mev;
					vev.events[0] = (VstEvent*) &mev;
					mev.type = kVstMidiType;
					mev.byteSize = sizeof (VstMidiEvent);
					mev.deltaFrames = ev->time.frames;
					memcpy (mev.midiData, (const uint8_t*)(ev+1), ev->body.size * sizeof (uint8_t));
					send_events_to_host (&vev);
				}
				ev = (LV2_Atom_Event const*) /* lv2_atom_sequence_next() */
					((const uint8_t*)ev + sizeof (LV2_Atom_Event) + ((ev->body.size + 7) & ~7));
			}
		}
	}

	/* signal worker end of process run */
	if (_worker) {
		_worker->end_run ();
	}
}
