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
#include "worker.h"

#ifdef _WIN32
#include <windows.h>
#endif

static void* worker_func (void* data)
{
	Lv2Worker* self =  (Lv2Worker*)data;
	self->run ();
	return NULL;
}

static LV2_Worker_Status lv2_worker_respond (
		LV2_Worker_Respond_Handle handle,
		uint32_t                  size,
		const void*               data)
{
	Lv2Worker* self = (Lv2Worker*) handle;
	return self->respond (size, data);
}

Lv2Worker::Lv2Worker (const LV2_Worker_Interface* iface, LV2_Handle handle)
	: _requests (4096)
	, _responses (4096)
	, _iface (iface)
	, _handle (handle)
	, _run (false)
	, _freewheeling (false)
{
	pthread_mutex_init (&_lock, NULL);
	pthread_cond_init (&_ready, NULL);
	pthread_create (&_thread, NULL, worker_func, this);
	while (!_run) {
#ifdef __APPLE__
		sched_yield ();
#elif defined _WIN32
		Sleep (1);
#else
		pthread_yield ();
#endif
	}
}

Lv2Worker::~Lv2Worker ()
{
	pthread_mutex_lock (&_lock);
	_run = false;
	pthread_cond_signal (&_ready);
	pthread_mutex_unlock (&_lock);

	pthread_join (_thread, NULL);
	pthread_mutex_destroy (&_lock);
	pthread_cond_destroy (&_ready);
}

LV2_Worker_Status Lv2Worker::schedule (uint32_t size, const void* data)
{
	if (_freewheeling) {
		_iface->work (_handle, lv2_worker_respond, this, size, data);
		return LV2_WORKER_SUCCESS;
	}
	_requests.write ((const char*)&size, sizeof (size));
	_requests.write ((const char*)data, size);
	if (pthread_mutex_trylock (&_lock) == 0) {
		pthread_cond_signal (&_ready);
		pthread_mutex_unlock (&_lock);
	}
	return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status Lv2Worker::respond (uint32_t size, const void* data)
{
	if (_responses.write_space () >= sizeof (size) + size) {
		_responses.write ((const char*)&size, sizeof (size));
		_responses.write ((const char*)data, size);
	}
	return LV2_WORKER_SUCCESS;
}

void Lv2Worker::emit_response ()
{
	uint32_t read_space = _responses.read_space ();
	while (read_space) {
		uint32_t size = 0;
		char worker_response[4096];
		_responses.read ((char*)&size, sizeof (size));
		_responses.read (worker_response, size);
		_iface->work_response (_handle, size, worker_response);
		read_space -= sizeof (size) + size;
	}
}

void Lv2Worker::run ()
{
	pthread_mutex_lock (&_lock);
	_run = true;
	while (1) {
		char buf[4096];
		uint32_t size = 0;

		if (_requests.read_space () <= sizeof (size)) {
			pthread_cond_wait (&_ready, &_lock);
		}

		if (!_run) {
			break;
		}

		_requests.read ((char*)&size, sizeof (size));

		if (size > 4096) {
			fprintf (stderr, "LV2Host: Worker information is too large. Abort.\n");
			break;
		}

		_requests.read (buf, size);
		_iface->work (_handle, lv2_worker_respond, this, size, buf);
	}
	pthread_mutex_unlock (&_lock);
}
