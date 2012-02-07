# Vumble-B

This project is designed to create a simple bridge between [Mumble](http://mumble.sourceforge.net/) and [Ventrilo](http://www.ventrilo.com/)

It uses libventrilo3 from [Mangler](http://www.mangler.org/) to interface with Ventrilo, and [libmumbleclient](https://github.com/pcgod/libmumbleclient) to interface
with Mumble

## Heads Up
 
This program does not currently function, if you can satisfy the build requirements of both libraries, it currently just connects
to each server and does nothing else. I'm mostly stashing it here as a starting point to others, and so that I can pick away at it
whenever I get free time

## Design

The intent is for a quick and dirty bridge between the two VOIP clients, It acts as a single user on each server and mixes recieved
voice data from all users into a single channel, which it then outputs on the opposite server, A more complex but technically correct
approach would be to have a connection mirror for each user connected, but I don't feel this is worth the effort. This program is simply
a stopgap to allow people to use Mumble, which is vastly superior to Ventrilo, while still being able to interact with legacy users for
short periods of time.

## Contributing

* The major area that needs improvement, is the functionality of libmumbleclient, currently its support for the mumble protocol is only
for connecting and sending text messages, everything else it does by playing back or recording raw udp/tcp streams as tunneled in the protocol,
this means it cannot natively identify specific protocols, different speakers, etc. which makes transcoding audio, filtering, etc. very difficult.
* The next major area would be libventrilo3, its API is a bit clunky, but mostly it's just an entirely different coding style than libmumbleclient, 
specifically it's C style functional programming, while libmumbleclient is OO C++, this makes interfacing them slightly clunky, an OO wrapper over
libventrilo3 would be nice. (It had to be patched to even link against C++ code)
* Both libraries are extracted from parent projects, libventrilo3 reflects this in that I had to strip out the autoconf build system that integrated
it with its parent, both libraries have a small mountain of dependencies, having them broken out into their own independant projects with their own
build systems would be very useful

