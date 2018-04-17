/*
 * osc2midi - a bridge between OSC and (ALSA) MIDI.
 * Copyright (C) 2018  Vilniaus Blokas UAB, https://blokas.io/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef MIDI_SERIALIZATION_H
#define MIDI_SERIALIZATION_H

typedef unsigned char uint8_t;

struct midi_event_t
{
	uint8_t m_event;
	uint8_t m_data[3];
};

#ifdef __cplusplus

class MidiToUsb
{
public:
	explicit MidiToUsb(int cable);

	void setCable(int cable);
	int getCable() const;

	bool process(uint8_t byte, midi_event_t &out);

private:
	int m_cable;

	uint8_t m_status;
	uint8_t m_data[3];

	uint8_t m_counter;
	bool m_sysex;
};

class UsbToMidi
{
public:
	static unsigned process(midi_event_t in, uint8_t out[3]);
};

#endif // __cplusplus

#endif // MIDI_SERIALIZATION_H
