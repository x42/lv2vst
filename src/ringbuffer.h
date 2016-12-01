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

#ifndef _ringbuffer_h_
#define _ringbuffer_h_

#include <cstring> // memcpy
#include <stdint.h>

#ifdef __clang__
# if __has_feature(cxx_atomic)
#  define CLANG_CXX11_ATOMICS 1
# endif
#endif

#ifdef CLANG_CXX11_ATOMICS
#include <atomic>

#define _atomic_int_get(P)    (P)
#define _atomic_int_set(P, V) (P) = (V)
#define adef std::atomic<uint32_t>
#define avar std::atomic<uint32_t>

#elif defined __ATOMIC_SEQ_CST && !defined __clang__

#define _atomic_int_get(P)    __atomic_load_4 (&(P), __ATOMIC_SEQ_CST)
#define _atomic_int_set(P, V) __atomic_store_4 (&(P), (V), __ATOMIC_SEQ_CST)

#define adef uint32_t
#define avar uint32_t

#elif defined __GNUC__

#define _atomic_int_set(P,V) __sync_lock_test_and_set (&(P), (V))
#define _atomic_int_get(P)   __sync_add_and_fetch (&(P), 0)
#define adef volatile uint32_t
#define avar uint32_t

#else

#warning non-atomic ringbuffer

#define _atomic_int_set(P,V) P = (V)
#define _atomic_int_get(P) P
#define adef size_t
#define avar size_t

#endif

namespace Lv2VstUtil {

template<class T> class RingBuffer
{
	public:
		RingBuffer (size_t s) {
			size = s;
			buf = new T[size];
			reset ();
		}

		virtual ~RingBuffer () {
			delete [] buf;
		}

		void reset () {
			_atomic_int_set (write_ptr, 0);
			_atomic_int_set (read_ptr, 0);
		}

		size_t read  (T *dest, size_t cnt);
		size_t write (const T *src, size_t cnt);

		size_t write_space () {
			size_t w, r;

			w = _atomic_int_get (write_ptr);
			r = _atomic_int_get (read_ptr);

			if (w > r) {
				return ((r - w + size) % size) - 1;
			} else if (w < r) {
				return (r - w) - 1;
			} else {
				return size - 1;
			}
		}

		size_t read_space () {
			avar w;
			avar r;

			w = _atomic_int_get (write_ptr);
			r = _atomic_int_get (read_ptr);

			if (w > r) {
				return w - r;
			} else {
				return (w - r + size) % size;
			}
		}

	protected:
		T *buf;
		size_t size;
		adef write_ptr;
		adef read_ptr;
};

template<class T> size_t RingBuffer<T>::read (T *dest, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_read;
	size_t n1, n2;
	size_t my_read_ptr;

	my_read_ptr = _atomic_int_get (read_ptr);

	if ((free_cnt = read_space ()) == 0) {
		return 0;
	}

	to_read = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = my_read_ptr + to_read;

	if (cnt2 > size) {
		n1 = size - my_read_ptr;
		n2 = cnt2 % size;
	} else {
		n1 = to_read;
		n2 = 0;
	}

	memcpy (dest, &buf[my_read_ptr], n1 * sizeof (T));
	my_read_ptr = (my_read_ptr + n1) % size;

	if (n2) {
		memcpy (dest+n1, buf, n2 * sizeof (T));
		my_read_ptr = n2;
	}

	_atomic_int_set (read_ptr, my_read_ptr);
	return to_read;
}

template<class T> size_t RingBuffer<T>::write (const T *src, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_write;
	size_t n1, n2;
	size_t my_write_ptr;

	my_write_ptr = _atomic_int_get (write_ptr);

	if ((free_cnt = write_space ()) == 0) {
		return 0;
	}

	to_write = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = my_write_ptr + to_write;

	if (cnt2 > size) {
		n1 = size - my_write_ptr;
		n2 = cnt2 % size;
	} else {
		n1 = to_write;
		n2 = 0;
	}

	memcpy (&buf[my_write_ptr], src, n1 * sizeof (T));
	my_write_ptr = (my_write_ptr + n1) % size;

	if (n2) {
		memcpy (buf, src+n1, n2 * sizeof (T));
		my_write_ptr = n2;
	}

	_atomic_int_set (write_ptr, my_write_ptr);
	return to_write;
}

} /* namespace */

#endif
