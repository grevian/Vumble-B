default: all

all:
	gcc -pthread -I ./libventrilo3/ vumble.c libventrilo3/libventrilo3_message.o libventrilo3/ventrilo3_handshake.o libventrilo3/libventrilo3.o -lm -o vumble
	g++ mumbleclient_test.cc -o mct -rdynamic ./libmumbleclient/libmumbleclient.a -I libmumbleclient/ -pthread -lcelt0 -lprotobuf -lssl -lboost_regex-mt -lboost_date_time-mt  -lboost_thread-mt -lboost_system-mt
