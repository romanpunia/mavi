#!/bin/bashif [ -x "$(command -v vcpkg)" ]; then	vcpkg install zlib	exit 0fiif [ -x "$(command -v apt-get)" ]; then	sudo apt-get install zlib1g-dev	exit 0fiif [ -x "$(command -v brew)" ]; then	sudo brew install zlib && ln -s /usr/local/opt/zlib/include/* /usr/local/include && ln -s /usr/local/opt/zlib/lib/*.dylib /usr/local/lib && ln -s /usr/local/opt/zlib/lib/*.a /usr/local/lib	exit 0fiecho "install cannot be done automatically"exit 1