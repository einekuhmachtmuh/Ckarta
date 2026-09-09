JDK_HOME ?= $(shell dirname $(shell dirname $(shell readlink -f $(shell command -v javac))))
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -I$(JDK_HOME)/include -I$(JDK_HOME)/include/linux
LDFLAGS += -L$(JDK_HOME)/lib/server -Wl,-rpath,$(JDK_HOME)/lib/server
LDLIBS += -ljvm -lpthread

all: build/ckarta-jni-thread-bench build/classes/org/ckarta/bench/BenchmarkTarget.class

build:
	mkdir -p build/classes

build/classes/org/ckarta/bench/BenchmarkTarget.class: BenchmarkTarget.java | build
	javac --release 21 -d build/classes BenchmarkTarget.java

build/ckarta-jni-thread-bench: jni_thread_benchmark.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean
