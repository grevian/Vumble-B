#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>
#include <sys/types.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <fstream>
#include <boost/make_shared.hpp>
#include <boost/thread.hpp>

#include <celt/celt.h>

#include "client.h"
#include "client_lib.h"
#include "CryptState.h"
#include "messages.h"
#include "PacketDataStream.h"
#include "settings.h"

#include "ventrilo3.h"

namespace {

// Always 48000 for Mumble
const int32_t kSampleRate = 48000;

bool recording = false;
bool playback = false;

boost::thread *playback_thread = NULL;

int debug = 1;


inline int32_t pds_int_len(char* x) {
	if ((x[0] & 0x80) == 0x00) {
		return 1;
	} else if ((x[0] & 0xC0) == 0x80) {
		return 2;
	} else if ((x[0] & 0xF0) == 0xF0) {
		switch (x[0] & 0xFC) {
			case 0xF0:
				return 5;
			case 0xF4:
				return 9;
			case 0xF8:
				return pds_int_len(&x[1]) + 1;
			case 0xFC:
				return 1;
			default:
				return 1;
		}
	} else if ((x[0] & 0xF0) == 0xE0) {
		return 3;
	} else if ((x[0] & 0xE0) == 0xC0) {
		return 3;
	}

	return 0;
}

int scanPacket(char* data, int len) {
	int header = 0;
	int frames = 0;
	// skip flags
	int pos = 1;

	// skip session & seqnr
	pos += pds_int_len(&data[pos]);
	pos += pds_int_len(&data[pos]);

	bool valid = true;
	do {
		header = static_cast<unsigned char>(data[pos]);
		++pos;
		++frames;
		pos += (header & 0x7f);

		if (pos > len)
			valid = false;
	} while ((header & 0x80) && valid);

	if (valid) {
		return frames;
	} else {
		return -1;
	}
}

void playbackFunction(MumbleClient::MumbleClient* mc) {
	std::cout << "<< playback thread" << std::endl;

	std::ifstream fs("udptunnel.out", std::ios::binary);
	while (playback && !fs.eof()) {
		int l = 0;
		fs.read(reinterpret_cast<char *>(&l), 4);

		if (!l)
			break;

		char *buffer = static_cast<char *>(malloc(l));
//		std::cout << "len: " << l << " pos: " << fs.tellg() << std::endl;
		fs.read(buffer, l);

		int frames = scanPacket(buffer, l);
//		std::cout << "scan: " << frames << " " << ceil(1000.0 / 10 / frames) << std::endl;

		buffer[0] = MumbleClient::UdpMessageType::UDPVoiceCELTAlpha | 0;
		memcpy(&buffer[1], &buffer[2], l - 1);

#define TCP 0
#if TCP
		mc->SendRawUdpTunnel(buffer, l - 1);
#else
		mc->SendUdpMessage(buffer, l - 1);
#endif

		boost::this_thread::sleep(boost::posix_time::milliseconds(frames * 10));
		free(buffer);
	}

	fs.close();
	playback = false;
	std::cout << ">> playback thread" << std::endl;
}

void AuthCallback() {
	std::cout << "I'm authenticated" << std::endl;
}

void TextMessageCallback(const std::string& message, MumbleClient::MumbleClient* mc) {
	if (message == "record") {
		recording = true;
	} else if (message == "play") {
		if (playback == false) {
			playback = true;
			playback_thread = new boost::thread(playbackFunction, mc);
		}
	} else if (message == "stop") {
		recording = false;
		playback = false;
#ifndef NDEBUG
	} else if (message == "channellist") {
		mc->PrintChannelList();
	} else if (message == "userlist") {
		mc->PrintUserList();
#endif
	} else if (message == "quit") {
		mc->Disconnect();
	}

	std::cout << "TM: " << message << std::endl;

	if (message != "quit")
		mc->SetComment(message);
}

void RawUdpTunnelCallback(int32_t length, void* buffer) {
	if (!recording)
		return;

	std::fstream fs("udptunnel.out", std::fstream::app | std::fstream::out | std::fstream::binary);
	fs.write(reinterpret_cast<const char *>(&length), 4);
	fs.write(reinterpret_cast<char *>(buffer), length);
	fs.close();
}

struct RelayMessage {
	MumbleClient::MumbleClient* mc;
	const std::string message;
	RelayMessage(MumbleClient::MumbleClient* mc_, const std::string& message_) : mc(mc_), message(message_) { }
};

boost::condition_variable cond;
boost::mutex mut;
std::deque< boost::shared_ptr<RelayMessage> > relay_queue;

void RelayThread() {
	boost::unique_lock<boost::mutex> lock(mut);
	while (true) {
		while (relay_queue.empty()) {
			cond.wait(lock);
		}

		boost::shared_ptr<RelayMessage>& r = relay_queue.front();
		r->mc->SendRawUdpTunnel(r->message.data(), r->message.size());
		relay_queue.pop_front();
	}
}

void RelayTunnelCallback(int32_t length, void* buffer, MumbleClient::MumbleClient* mc) {
	std::string s(static_cast<char *>(buffer), length);
	s.erase(1, pds_int_len(&static_cast<char *>(buffer)[1]));
	boost::shared_ptr<RelayMessage> r = boost::make_shared<RelayMessage>(mc, s);
	{
		boost::lock_guard<boost::mutex> lock(mut);
		relay_queue.push_back(r);
	}
	cond.notify_all();
}

}  // namespace

bool keep_running = true;
void usage(char *argv[]);
void ctrl_c(int signum);
void main_loop(void *connptr, MumbleClient::MumbleClientLib* mc);


struct _conninfo {
    char *server;
    char *username;
    char *password;
    char *channelid;
};

void ctrl_c (int signum) {
    printf("disconnecting... ");
    v3_logout();
    keep_running = false;
    printf("done\n");
    exit(0);
}

void usage(char *argv[]) {
    fprintf(stderr, "usage: %s -h hostname:port -u username [-p password] -c channelid\n", argv[0]);
    exit(1);
}

void main_loop(void *connptr, MumbleClient::MumbleClientLib* mcl) {
    struct _conninfo *conninfo;
    _v3_net_message *msg;
    v3_event *ev;

    conninfo = connptr;
    if (debug >= 2) {
        v3_debuglevel(V3_DEBUG_PACKET | V3_DEBUG_PACKET_PARSE | V3_DEBUG_INFO);
    }
    if (! v3_login(conninfo->server, conninfo->username, conninfo->password, "")) {
        fprintf(stderr, "could not log in to ventrilo server: %s\n", _v3_error(NULL));
    }

    while (keep_running) {
        // Handle the Mumble Client
  	    mcl->Pump();

        // Handle incoming events
        msg = _v3_recv(V3_NONBLOCK);
        if ( msg != NULL ) {
            switch (_v3_process_message(msg)) {
                case V3_MALFORMED:
                    _v3_debug(V3_DEBUG_INFO, "received malformed packet");
                    break;
                case V3_NOTIMPL:
                    _v3_debug(V3_DEBUG_INFO, "packet type not implemented");
                    break;
                case V3_OK:
                    _v3_debug(V3_DEBUG_INFO, "packet processed");
                    break;
            }
            // free(msg); // Looks like process_message handles freeing the memory used
        }

        // Handle any outgoing Events
        ev = v3_get_event(V3_NONBLOCK);
        if ( ev != NULL ) {
            if (debug) {
                fprintf(stderr, "vumble: got event type %d\n", ev->type);
            }
            switch (ev->type) {
                case V3_EVENT_DISCONNECT:
                    keep_running = false;
                    break;
                case V3_EVENT_LOGIN_COMPLETE:
                    v3_change_channel(atoi(conninfo->channelid), "");
                    fprintf(stderr, "***********************************************************************************\n");
                    fprintf(stderr, "Connected to Ventrilo Server\n");
                    fprintf(stderr, "***********************************************************************************\n");
                    v3_serverprop_open();
                    break;
            }
            free(ev);
        }
    }
}


int main(int argc, char* argv[] ) {

    int opt;
    int rc;
    struct _conninfo conninfo;

    memset(&conninfo, 0, sizeof(struct _conninfo));
    while ((opt = getopt(argc, argv, "dh:p:u:c:")) != -1) {
        switch (opt) {
            case 'd':
                debug++;
                break;
            case 'h':
                conninfo.server = strdup(optarg);
                break;
            case 'u':
                conninfo.username = strdup(optarg);
                break;
            case 'c':
                conninfo.channelid = strdup(optarg);
                break;
            case 'p':
                conninfo.password = strdup(optarg);
                break;
        }
    }
    if (! conninfo.server)  {
        fprintf(stderr, "error: server hostname (-h) was not specified\n");
        usage(argv);
    }
    if (! conninfo.username)  {
        fprintf(stderr, "error: username (-u) was not specified\n");
        usage(argv);
    }
    if (! conninfo.channelid) {
        fprintf(stderr, "error: channel id (-c) was not specified\n");
        usage(argv);
    }
    if (! conninfo.password) {
        conninfo.password = "";
    }
    fprintf(stderr, "server: %s\nusername: %s\n", conninfo.server, conninfo.username);
    signal (SIGINT, ctrl_c);

	MumbleClient::MumbleClientLib* mcl = MumbleClient::MumbleClientLib::instance();

	MumbleClient::MumbleClientLib::SetLogLevel(1);

	// Create a new client
	MumbleClient::MumbleClient* mc = mcl->NewClient();

	mc->Connect(MumbleClient::Settings("mel.grevian.org", "64000", "testBot2", "GrevianVOIP"));

	mc->SetAuthCallback(boost::bind(&AuthCallback));
	mc->SetTextMessageCallback(boost::bind(&TextMessageCallback, _1, mc));
	mc->SetRawUdpTunnelCallback(boost::bind(&RawUdpTunnelCallback, _1, _2));

  main_loop((void*)&conninfo, mcl);

	delete mc;

	mcl->Shutdown();
	mcl = NULL;

}


