#!/bin/bashif [ -x "$(command -v vcpkg)" ]; then	vcpkg install openal-soft	exit 0fiif [ -x "$(command -v apt-get)" ]; then	sudo apt-get install libopenal-dev	exit 0fiif [ -x "$(command -v brew)" ]; then	sudo brew install openal-soft && ln -s /usr/local/opt/openal-soft/include/AL /usr/local/include && ln -s /usr/local/opt/openal-soft/lib/*.dylib /usr/local/lib && ln -s /usr/local/opt/openal-soft/lib/*.a /usr/local/lib	exit 0fiecho "install cannot be done automatically"exit 1