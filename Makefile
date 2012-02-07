default: all

all:
	g++ vumble.cpp -rdynamic ./libventrilo3/libventrilo.a -rdynamic ./libmumbleclient/libmumbleclient.a -I libventrilo3/ -I libmumbleclient/ -pthread -lcelt0 -lprotobuf -lssl -lboost_regex-mt -lboost_date_time-mt  -lboost_thread-mt -lboost_system-mt -lm -fpermissive -o vumble
