default: all

all: static

static:
	gcc -pthread -Wall -DNO_AUTOMAKE -c libventrilo3.c -o libventrilo3.o
	gcc -pthread -Wall -DNO_AUTOMAKE -c ventrilo3_handshake.c -o ventrilo3_handshake.o
	gcc -pthread -Wall -DNO_AUTOMAKE -c libventrilo3_message.c -o libventrilo3_message.o
	ar rcs libventrilo.a libventrilo3.o ventrilo3_handshake.o libventrilo3_message.o

shared:
	gcc -fPIC -pthread -Wall -DNO_AUTOMAKE -c libventrilo3.c -o libventrilo3.o
	gcc -fPIC -pthread -Wall -DNO_AUTOMAKE -c ventrilo3_handshake.c -o ventrilo3_handshake.o
	gcc -fPIC -pthread -Wall -DNO_AUTOMAKE -c libventrilo3_message.c -o libventrilo3_message.o
	gcc -shared -Wl,-soname,libventrilo.so.1 -o libventrilo.so.1.0.1 libventrilo3.o ventrilo3_handshake.o libventrilo3_message.o

clean:
	rm *.o
	rm libventrilo.so.1.0.1
	rm libventrilo.a


