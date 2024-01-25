# osc2midi - a bridge between OSC and (ALSA) MIDI.
# Copyright (C) 2018  Vilniaus Blokas UAB, https://blokas.io/
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2 of the
# License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

PREFIX ?= /usr/local
DESTDIR ?=
BINARY_DIR ?= $(DESTDIR)$(PREFIX)/bin

all: osc2midi

CXXFLAGS ?= -O3
LDFLAGS ?= -lasound

osc2midi: osc2midi.o midi_serialization.o
	$(CXX) $^ -o $@ -lasound
	strip $@

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $^ -o $@

install: all
	mkdir -p $(BINARY_DIR)
	@cp -p osc2midi $(BINARY_DIR)/

clean:
	rm -f osc2midi *.o
