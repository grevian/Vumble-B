default: all

all:
	gcc -pthread -I ./libventrilo3/ vumble.c libventrilo3/libventrilo3_message.o libventrilo3/ventrilo3_handshake.o libventrilo3/libventrilo3.o -lm -o vumble
