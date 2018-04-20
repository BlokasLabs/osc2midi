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

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <poll.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "midi_serialization.h"

#define HOMEPAGE_URL "https://blokas.io/"
#define OSC2MIDI_VERSION 0x0100

// Sent to the provided host in the command line arguments.
// The first argument is the port number, the 2nd is the given port name
// (also provided on command line).
//
// Example:
//
// /osc2midi/hello is 8000 "osc2midi"
static const char MSG_HELLO[] = {
	'/', 'o', 's', 'c', '2', 'm', 'i', 'd', 'i', '/', 'h', 'e', 'l', 'l', 'o', '\0',
	',', 'i', 's', '\0'
};

// This message is sent to the provided host whenever MIDI Input is received.
// To produce MIDI Output, this message should be sent to osc2midi service.
// The argument is a 32bit hex encoded as a string, it's based on USB MIDI format.
// http://www.usb.org/developers/docs/devclass_docs/midi10.pdf, Ch. 4
//
// Example:
// 
// /osc2midi/event s 09904030
static const char MSG_MIDI_EVENT[] = {
	'/', 'o', 's', 'c', '2', 'm', 'i', 'd', 'i', '/', 'e', 'v', 'e', 'n', 't', '\0',
	',', 's', '\0', '\0'
};

// This message can be sent by the host to osc2midi service to make it exit gracefully.
//
// Example:
//
// /osc2midi/bye
static const char MSG_BYE[] = {
	'/', 'o', 's', 'c', '2', 'm', 'i', 'd', 'i', '/', 'b', 'y', 'e', '\0', '\0', '\0'
};

static snd_seq_t *g_seq;
static int g_port;
static snd_midi_event_t *g_encoder;
static snd_midi_event_t *g_decoder;

static void seqUninit()
{
	if (g_encoder)
	{
		snd_midi_event_free(g_encoder);
		g_encoder = NULL;
	}
	if (g_decoder)
	{
		snd_midi_event_free(g_decoder);
		g_decoder = NULL;
	}
	if (g_port)
	{
		snd_seq_delete_simple_port(g_seq, g_port);
		g_port = 0;
	}
	if (g_seq)
	{
		snd_seq_close(g_seq);
		g_seq = NULL;
	}
}

static int seqInit(const char *portName)
{
	if (g_seq != NULL)
	{
		fprintf(stderr, "Already initialized!\n");
		return -EINVAL;
	}

	int result = snd_seq_open(&g_seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
	if (result < 0)
	{
		fprintf(stderr, "Couldn't open ALSA sequencer! (%d)\n", result);
		goto error;
	}

	result = snd_seq_set_client_name(g_seq, "osc2midi");
	if (result < 0)
	{
		fprintf(stderr, "Failed setting client name! (%d)\n", result);
		goto error;
	}

	result = snd_seq_create_simple_port(
		g_seq,
		portName,
		SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
		SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ |
		SND_SEQ_PORT_CAP_DUPLEX,
		SND_SEQ_PORT_TYPE_APPLICATION
		);

	if (result < 0)
	{
		fprintf(stderr, "Couldn't create a virtual MIDI port! (%d)\n", result);
		goto error;
	}

	g_port = result;

	result = snd_midi_event_new(32, &g_decoder);
	if (result < 0)
	{
		fprintf(stderr, "Failed creating MIDI decoder! (%d)\n", result);
		goto error;
	}

	result = snd_midi_event_new(32, &g_encoder);
	if (result < 0)
	{
		fprintf(stderr, "Failed creating MIDI encoder! (%d)\n", result);
		goto error;
	}

	return 0;

error:
	seqUninit();
	return result;
}

static size_t seqDecodeToMIDI(uint8_t *buffer, size_t bufferSize, const snd_seq_event_t *event)
{
	if (event->type == SND_SEQ_EVENT_PORT_SUBSCRIBED || event->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED)
		return 0;

	long n = snd_midi_event_decode(g_decoder, buffer, bufferSize, event);

	return n >= 0 ? n : 0;
}

static int sendHello(int socket, const sockaddr_in &addr, const char *name)
{
	sockaddr_in myAddr;
	socklen_t len = sizeof(myAddr);
	if (getsockname(socket, (sockaddr*)&myAddr, &len) < 0)
		return -errno;

	char buffer[256];

	memcpy(buffer, MSG_HELLO, sizeof(MSG_HELLO));
	*((uint32_t*)(buffer + sizeof(MSG_HELLO))) = htonl((uint32_t)ntohs(myAddr.sin_port));
	size_t n = strlen(name) + 1;
	if (n > (sizeof(buffer) - sizeof(MSG_HELLO) + sizeof(uint32_t)))
		return -EMSGSIZE;

	char *p = n + strncpy((buffer + sizeof(MSG_HELLO) + sizeof(uint32_t)), name, n);

	while ((intptr_t)p & 0x3)
		*p++ = '\0';

	return sendto(socket, buffer, p - buffer, 0, (const sockaddr*)&addr, sizeof(addr));
}

static char * encodeHex32(char *dst, uint32_t d)
{
	static const char HEX[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	for (int i=0; i<7; ++i)
	{
		*dst++ = HEX[(d & 0xf0000000) >> 28];
		d <<= 4;
	}
	*dst++ = HEX[(d & 0xf0000000) >> 28];
	*dst = '\0';
	return dst;
}

static bool decodeHex32(uint32_t &result, const char *src)
{
	result = 0;

	for (int i=0; i<8 && src[i]; ++i)
	{
		char c = src[i];
		if (c >= '0' && c <= '9')
		{
			result = (result << 4) | (c - '0');
		}
		else if (c >= 'a' && c <= 'f')
		{
			result = (result << 4) | (c - 'a' + 10);
		}
		else if (c >= 'A' && c <= 'F')
		{
			result = (result << 4) | (c - 'A' + 10);
		}
		else
		{
			result = 0;
			return false;
		}
	}

	return true;
}

static int sendMidiEvent(int socket, const sockaddr_in &addr, const midi_event_t &event)
{
	char buffer[32];
	memcpy(buffer, MSG_MIDI_EVENT, sizeof(MSG_MIDI_EVENT));

	char *p = encodeHex32(buffer + sizeof(MSG_MIDI_EVENT), ((event.m_event) << 24) | ((event.m_data[0]) << 16) | ((event.m_data[1]) << 8) | event.m_data[2]) + 1;

	*p++ = '\0';
	*p++ = '\0';
	*p++ = '\0';

	assert(p-buffer == sizeof(buffer));

	return sendto(socket, buffer, p - buffer, 0, (const sockaddr*)&addr, sizeof(addr));
}

static int g_socket = 0;

static int udpInit()
{
	if (g_socket != 0)
	{
		fprintf(stderr, "UDP socket already initialized!\n");
		return -EINVAL;
	}

	int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
	{
		fprintf(stderr, "Failed creating a UDP socket! (%d)\n", errno);
		return -errno;
	}

	sockaddr_in myAddr;
	memset(&myAddr, 0, sizeof(myAddr));
	myAddr.sin_family = AF_INET;
	myAddr.sin_port = 0;
	myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (sockaddr*)&myAddr, sizeof(myAddr)) < 0)
	{
		fprintf(stderr, "Failed binding the UDP socket! (%d)\n", errno);
		close(s);
		return -errno;
	}

	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		fprintf(stderr, "Failed making UDP socket non-blocking! (%d)\n", errno);
		close(s);
		return -errno;
	}

	g_socket = s;

	return 0;
}

static void udpUninit()
{
	if (g_socket != 0)
	{
		close(g_socket);
		g_socket = 0;
	}
}

static bool handleUdpPacket(const char *buffer, size_t len, snd_seq_t *seq, int portId)
{
	if (memcmp(buffer, MSG_MIDI_EVENT, sizeof(MSG_MIDI_EVENT)) == 0)
	{
		if (len < sizeof(MSG_MIDI_EVENT) + 12)
			return false;

		uint32_t t;
		if (decodeHex32(t, buffer + sizeof(MSG_MIDI_EVENT)))
		{
			midi_event_t midiEvent;
			midiEvent.m_event = t >> 24;
			midiEvent.m_data[0] = (t >> 16) & 0xff;
			midiEvent.m_data[1] = (t >> 8) & 0xff;
			midiEvent.m_data[2] = t & 0xff;
			uint8_t rawMidi[3];
			unsigned l = UsbToMidi::process(midiEvent, rawMidi);
			if (l > 0 && l <= 3)
			{
				snd_midi_event_t *e;
				snd_midi_event_new(64, &e);
				snd_seq_event_t ev;
				snd_seq_ev_clear(&ev);
				snd_seq_ev_set_source(&ev, portId);
				snd_seq_ev_set_subs(&ev);
				snd_seq_ev_set_direct(&ev);
				snd_midi_event_encode(e, rawMidi, l, &ev);
				snd_seq_event_output_direct(seq, &ev);
				snd_midi_event_free(e);
			}
		}
		return false;
	}
	else if (memcmp(buffer, MSG_BYE, sizeof(MSG_BYE)) == 0)
	{
		return true;
	}

	return false;
}

static MidiToUsb g_midiToUsb = MidiToUsb(0);

static bool handleSeqEvent(snd_seq_t *seq, const sockaddr_in &addr, int portId)
{
	do
	{
		snd_seq_event_t *ev;
		snd_seq_event_input(seq, &ev);
		uint8_t buffer[64];
		size_t len = seqDecodeToMIDI(buffer, sizeof(buffer), ev);
		for (size_t i=0; i<len; ++i)
		{
			midi_event_t midiEvent;
			if (g_midiToUsb.process(buffer[i], midiEvent))
			{
				sendMidiEvent(g_socket, addr, midiEvent);
			}
		}
		snd_seq_free_event(ev);
	} while (snd_seq_event_input_pending(seq, 0) > 0);

	return false;
}

static int run(const char *name, const char *ip, uint16_t port)
{
	if (!name || !ip)
		return -EINVAL;

	bool done = false;
	int npfd = 0;

	int result = seqInit(name);

	if (result < 0)
		goto cleanup;

	result = udpInit();

	if (result < 0)
		goto cleanup;

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	if (inet_aton(ip, &addr.sin_addr) == 0)
	{
		fprintf(stderr, "Invalid address provided: '%s'\n", ip);
		result = -EINVAL;
		goto cleanup;
	}
	addr.sin_port = ntohs(port);

	sendHello(g_socket, addr, name);

	npfd = snd_seq_poll_descriptors_count(g_seq, POLLIN);
	if (npfd != 1)
	{
		fprintf(stderr, "Unexpected count (%d) of seq fds! Expected 1!", npfd);
		result = -EINVAL;
		goto cleanup;
	}

	pollfd fds[2];
	snd_seq_poll_descriptors(g_seq, &fds[0], 1, POLLIN);
	fds[1].fd = g_socket;
	fds[1].events = POLLIN;

	while (!done)
	{
		int n = poll(fds, 2, -1);
		if (n < 0)
		{
			fprintf(stderr, "Polling failed! (%d)\n", errno);
			result = -errno;
			goto cleanup;
		}

		if (fds[0].revents)
		{
			--n;
			done = handleSeqEvent(g_seq, addr, g_port);
		}
		if (fds[1].revents)
		{
			--n;
			char buffer[256];
			sockaddr_in a;
			socklen_t l = sizeof(a);
			ssize_t len = recvfrom(g_socket, buffer, sizeof(buffer), 0, (sockaddr*)&a, &l);
			if (len > 0)
			{
				done = handleUdpPacket(buffer, (size_t)len, g_seq, g_port);
			}
		}

		assert(n == 0);
	}

cleanup:
	udpUninit();
	seqUninit();

	return result;
}

static void printVersion(void)
{
	printf("Version %x.%02x, Copyright (C) Blokas Labs " HOMEPAGE_URL "\n", OSC2MIDI_VERSION >> 8, OSC2MIDI_VERSION & 0xff);
}

static void printUsage()
{
	printf("Usage: osc2midi \"Virtual Port Name\" host_ip host_port\n"
		"Example:\n"
		"\tosc2midi \"Osc MIDI Bridge\" 127.0.0.1 8000\n"
		"\n"
		);
	printVersion();
}

int main(int argc, char **argv)
{
	if (argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0))
	{
		printVersion();
		return 0;
	}
	else if (argc != 4)
	{
		printUsage();
		return 0;
	}

	char *endPtr;
	uint32_t port = strtoul(argv[3], &endPtr, 10);

	if (endPtr == argv[3] || *endPtr != '\0')
	{
		fprintf(stderr, "Failed parsing host_port argument!\n");
		return EINVAL;
	}

	if (port == 0 || port >= 65536)
	{
		fprintf(stderr, "Port argument is out of range! Valid range is 1 <-> 65535.\n");
		return EINVAL;
	}

	int result = run(argv[1], argv[2], port);

	if (result < 0)
		fprintf(stderr, "Error %d!\n", result);

	return result;
}
