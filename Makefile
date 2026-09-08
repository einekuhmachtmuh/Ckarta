JAVA_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v javac))))
BUILD_DIR := build
CLASS_DIR := $(BUILD_DIR)/classes
BIN_DIR := $(BUILD_DIR)/bin
CLASS_STAMP := $(CLASS_DIR)/.stamp
TARGET := $(BIN_DIR)/ckarta-smoke
ABI_TEST := $(BIN_DIR)/ckarta-request-lifecycle-test

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

$(TARGET): c/core/main.c c/jni/ck_jni_runtime.c c/jni/ck_jni_runtime.h c/jni/ck_request.c c/jni/ck_request.h c/jni/ck_completion.c c/jni/ck_completion.h classes
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) c/core/main.c c/jni/ck_jni_runtime.c c/jni/ck_request.c c/jni/ck_completion.c -o $@ $(LDFLAGS)

$(ABI_TEST): tests/request_lifecycle_test.c c/jni/ck_request.c c/jni/ck_request.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/request_lifecycle_test.c c/jni/ck_request.c -o $@

clean:
	rm -rf $(BUILD_DIR)

test: all $(ABI_TEST)
	$(ABI_TEST)
	./tests/smoke_bootstrap.sh $(TARGET)
