#include <celt/celt.h>
#include <iostream>
#include <fstream>
#include <boost/make_shared.hpp>
#include <boost/thread.hpp>

#include "client.h"
#include "client_lib.h"
#include "CryptState.h"
#include "messages.h"
#include "PacketDataStream.h"
#include "settings.h"

namespace {

// Always 48000 for Mumble
const int32_t kSampleRate = 48000;

bool recording = false;
bool playback = false;

boost::thread *playback_thread = NULL;

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

int main(int /* argc */, char** /* argv[] */) {
	MumbleClient::MumbleClientLib* mcl = MumbleClient::MumbleClientLib::instance();

	MumbleClient::MumbleClientLib::SetLogLevel(1);

	// Create a new client
	MumbleClient::MumbleClient* mc = mcl->NewClient();

	mc->Connect(MumbleClient::Settings("mel.grevian.org", "64000", "testBot2", "GrevianVOIP"));

	mc->SetAuthCallback(boost::bind(&AuthCallback));
	mc->SetTextMessageCallback(boost::bind(&TextMessageCallback, _1, mc));
	mc->SetRawUdpTunnelCallback(boost::bind(&RawUdpTunnelCallback, _1, _2));

	//mc->SetRawUdpTunnelCallback(boost::bind(&RelayTunnelCallback, _1, _2, mc2));
	//mc2->SetRawUdpTunnelCallback(boost::bind(&RelayTunnelCallback, _1, _2, mc));

	//boost::thread relay_thread = boost::thread(RelayThread);

	// Start event loop
	mcl->Run();
	delete mc;

	mcl->Shutdown();
	mcl = NULL;
}
