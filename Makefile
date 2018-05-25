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

BINARY_DIR ?= /usr/local/bin

all: osc2midi

CXXFLAGS ?= -O3
LDFLAGS ?= -lasound

CXX=g++-4.9

osc2midi: osc2midi.o midi_serialization.o
	$(CXX) $^ -o $@ -lasound
	strip $@

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $^ -o $@

install: all
	@cp -p osc2midi $(BINARY_DIR)/

clean:
	rm -f osc2midi *.o
	rm -f osc2midi.deb
	rm -f debian/usr/bin/osc2midi
	gunzip `find . | grep gz` > /dev/null 2>&1 || true

osc2midi.deb: osc2midi
	@gzip --best -n ./debian/usr/share/doc/osc2midi/changelog ./debian/usr/share/doc/osc2midi/changelog.Debian ./debian/usr/share/man/man1/osc2midi.1
	@mkdir -p debian/usr/bin
	@cp -p osc2midi debian/usr/bin/
	@fakeroot dpkg --build debian
	@mv debian.deb osc2midi.deb
	@gunzip `find . | grep gz` > /dev/null 2>&1
