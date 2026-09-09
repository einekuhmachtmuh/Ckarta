JAVA_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v javac))))
BUILD_DIR := build
CLASS_DIR := $(BUILD_DIR)/classes
BIN_DIR := $(BUILD_DIR)/bin
CLASS_STAMP := $(CLASS_DIR)/.stamp
TARGET := $(BIN_DIR)/ckarta-smoke
ABI_TEST := $(BIN_DIR)/ckarta-request-lifecycle-test
CONFIG_TEST := $(BIN_DIR)/ckarta-config-test
ERROR_TEST := $(BIN_DIR)/ckarta-error-test
ERROR_RACE_TEST := $(BIN_DIR)/ckarta-request-error-race-test
TERMINAL_RACE_TEST := $(BIN_DIR)/ckarta-request-terminal-race-test
COMPLETION_QUEUE_TEST := $(BIN_DIR)/ckarta-completion-queue-test

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

$(TARGET): c/core/main.c c/config/ck_config.c c/config/ck_config.h c/error/ck_error.c c/error/ck_error.h c/completion/ck_completion_queue.c c/completion/ck_completion_queue.h c/event/ck_completion_notification.c c/event/ck_completion_notification.h c/jni/ck_jni_runtime.c c/jni/ck_jni_runtime.h c/jni/ck_request.c c/jni/ck_request.h classes
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) c/core/main.c c/config/ck_config.c c/error/ck_error.c c/completion/ck_completion_queue.c c/event/ck_completion_notification.c c/jni/ck_jni_runtime.c c/jni/ck_request.c -o $@ $(LDFLAGS)

$(ABI_TEST): tests/request_lifecycle_test.c c/jni/ck_request.c c/jni/ck_request.h c/error/ck_error.c c/error/ck_error.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/request_lifecycle_test.c c/jni/ck_request.c c/error/ck_error.c -o $@

$(ERROR_TEST): tests/error/ck_error_test.c c/error/ck_error.c c/error/ck_error.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/error/ck_error_test.c c/error/ck_error.c -o $@

$(ERROR_RACE_TEST): tests/error/request_error_race_test.c c/jni/ck_request.c c/jni/ck_request.h c/error/ck_error.c c/error/ck_error.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/error/request_error_race_test.c c/jni/ck_request.c c/error/ck_error.c -o $@

$(TERMINAL_RACE_TEST): tests/error/request_terminal_race_test.c c/jni/ck_request.c c/jni/ck_request.h c/error/ck_error.c c/error/ck_error.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/error/request_terminal_race_test.c c/jni/ck_request.c c/error/ck_error.c -o $@

$(COMPLETION_QUEUE_TEST): tests/completion/ck_completion_queue_test.c c/completion/ck_completion_queue.c c/completion/ck_completion_queue.h c/event/ck_completion_notification.c c/event/ck_completion_notification.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/completion/ck_completion_queue_test.c c/completion/ck_completion_queue.c c/event/ck_completion_notification.c -o $@

$(CONFIG_TEST): tests/config_load_test.c c/config/ck_config.c c/config/ck_config.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/config_load_test.c c/config/ck_config.c -o $@

clean:
	rm -rf $(BUILD_DIR)

test: all $(ABI_TEST) $(CONFIG_TEST) $(ERROR_TEST) $(ERROR_RACE_TEST) $(TERMINAL_RACE_TEST) $(COMPLETION_QUEUE_TEST)
	$(ABI_TEST)
	$(CONFIG_TEST) tests/config/valid.conf
	$(ERROR_TEST)
	$(ERROR_RACE_TEST)
	$(TERMINAL_RACE_TEST)
	$(COMPLETION_QUEUE_TEST)
	! $(CONFIG_TEST) tests/config/invalid.conf
	./tests/smoke_bootstrap.sh $(TARGET)
