JAVA_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v javac))))
BUILD_DIR := build
CLASS_DIR := $(BUILD_DIR)/classes
BIN_DIR := $(BUILD_DIR)/bin
CLASS_STAMP := $(CLASS_DIR)/.stamp
TARGET := $(BIN_DIR)/ckarta-smoke

CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -pthread
CPPFLAGS := -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux
LDFLAGS := -L$(JAVA_HOME)/lib/server -Wl,-rpath,$(JAVA_HOME)/lib/server -ljvm -pthread

JAVA_SOURCES := $(shell find java -name '*.java' -print)

.PHONY: all classes clean test

all: $(TARGET)

classes: $(CLASS_STAMP)

$(CLASS_STAMP): $(JAVA_SOURCES)
	@mkdir -p $(CLASS_DIR)
	javac --release 21 -d $(CLASS_DIR) $(JAVA_SOURCES)
	@touch $@

$(TARGET): c/core/main.c c/jni/ck_jni_runtime.c c/jni/ck_jni_runtime.h classes
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) c/core/main.c c/jni/ck_jni_runtime.c -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

test: all
	./tests/smoke_bootstrap.sh $(TARGET)
