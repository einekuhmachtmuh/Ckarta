JAVA_HOME ?= $(shell dirname $$(dirname $$(readlink -f $$(command -v javac))))
BUILD_DIR := build
CLASS_DIR := $(BUILD_DIR)/classes
BIN_DIR := $(BUILD_DIR)/bin
CLASS_STAMP := $(CLASS_DIR)/.stamp
JAKARTA_SERVLET_API_VERSION := 6.1.0
JAKARTA_SERVLET_API_JAR := $(BUILD_DIR)/deps/jakarta.servlet-api-$(JAKARTA_SERVLET_API_VERSION).jar
JAKARTA_SERVLET_API_URL := https://repo1.maven.org/maven2/jakarta/servlet/jakarta.servlet-api/$(JAKARTA_SERVLET_API_VERSION)/jakarta.servlet-api-$(JAKARTA_SERVLET_API_VERSION).jar
JAKARTA_SERVLET_API_SHA256 := 8a31f465f3593bf2351531a5c952014eb839da96a605b5825b93dd54714c48c4
TARGET := $(BIN_DIR)/ckarta-smoke
JAVA_ASYNC_TEST := $(BIN_DIR)/ckarta-async-context-test
JAVA_SERVLET_API_TEST := $(BIN_DIR)/ckarta-servlet-async-api-test
JAVA_SERVLET_REQUEST_ASYNC_TEST := $(BIN_DIR)/ckarta-servlet-request-async-test
ABI_TEST := $(BIN_DIR)/ckarta-request-lifecycle-test
CONFIG_TEST := $(BIN_DIR)/ckarta-config-test
ERROR_TEST := $(BIN_DIR)/ckarta-error-test
ERROR_RACE_TEST := $(BIN_DIR)/ckarta-request-error-race-test
TERMINAL_RACE_TEST := $(BIN_DIR)/ckarta-request-terminal-race-test
COMPLETION_QUEUE_TEST := $(BIN_DIR)/ckarta-completion-queue-test
CONNECTION_TEST := $(BIN_DIR)/ckarta-connection-test

CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -pthread
CPPFLAGS := -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux
LDFLAGS := -L$(JAVA_HOME)/lib/server -Wl,-rpath,$(JAVA_HOME)/lib/server -ljvm -pthread

JAVA_SOURCES := $(shell find java -name '*.java' -print)

.PHONY: all classes clean test

all: $(TARGET)

classes: $(CLASS_STAMP)

$(JAKARTA_SERVLET_API_JAR):
	@mkdir -p $(BUILD_DIR)/deps
	curl --fail --location --silent --show-error $(JAKARTA_SERVLET_API_URL) -o $@.tmp
	printf "%s  %s\n" "$(JAKARTA_SERVLET_API_SHA256)" "$@.tmp" | sha256sum --check --status -
	mv $@.tmp $@

$(CLASS_STAMP): $(JAVA_SOURCES) $(JAKARTA_SERVLET_API_JAR)
	@mkdir -p $(CLASS_DIR)
	javac --release 21 -cp $(JAKARTA_SERVLET_API_JAR) -d $(CLASS_DIR) $(JAVA_SOURCES)
	@touch $@

$(TARGET): c/core/main.c c/config/ck_config.c c/config/ck_config.h c/error/ck_error.c c/error/ck_error.h c/completion/ck_completion_queue.c c/completion/ck_completion_queue.h c/event/ck_completion_notification.c c/event/ck_completion_notification.h c/connection/ck_connection.c c/connection/ck_connection.h c/jni/ck_jni_runtime.c c/jni/ck_jni_runtime.h c/jni/ck_request.c c/jni/ck_request.h classes
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) c/core/main.c c/config/ck_config.c c/error/ck_error.c c/completion/ck_completion_queue.c c/event/ck_completion_notification.c c/connection/ck_connection.c c/jni/ck_jni_runtime.c c/jni/ck_request.c -o $@ $(LDFLAGS)


$(JAVA_SERVLET_REQUEST_ASYNC_TEST): tests/java/CkartaServletRequestAsyncTest.java java/org/ckarta/servlet/CkartaServletRequestAdapter.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java $(JAKARTA_SERVLET_API_JAR)
	@mkdir -p $(BUILD_DIR)/java-test-classes
	javac --release 21 -cp $(JAKARTA_SERVLET_API_JAR) -d $(BUILD_DIR)/java-test-classes tests/java/CkartaServletRequestAsyncTest.java java/org/ckarta/servlet/CkartaServletRequestAdapter.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java

$(JAVA_ASYNC_TEST): tests/java/CkartaAsyncContextTest.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java
	@mkdir -p $(BUILD_DIR)/java-test-classes
	javac --release 21 -d $(BUILD_DIR)/java-test-classes $^

$(JAVA_SERVLET_API_TEST): tests/java/CkartaServletAsyncContextTest.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java $(JAKARTA_SERVLET_API_JAR)
	@mkdir -p $(BUILD_DIR)/java-test-classes
	javac --release 21 -cp $(JAKARTA_SERVLET_API_JAR) -d $(BUILD_DIR)/java-test-classes tests/java/CkartaServletAsyncContextTest.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java

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

$(CONNECTION_TEST): tests/connection/ck_connection_test.c c/connection/ck_connection.c c/connection/ck_connection.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/connection/ck_connection_test.c c/connection/ck_connection.c -o $@

$(CONFIG_TEST): tests/config_load_test.c c/config/ck_config.c c/config/ck_config.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/config_load_test.c c/config/ck_config.c -o $@

clean:
	rm -rf $(BUILD_DIR)

test: all $(JAVA_ASYNC_TEST) $(JAVA_SERVLET_API_TEST) $(JAVA_SERVLET_REQUEST_ASYNC_TEST) $(ABI_TEST) $(CONFIG_TEST) $(ERROR_TEST) $(ERROR_RACE_TEST) $(TERMINAL_RACE_TEST) $(COMPLETION_QUEUE_TEST) $(CONNECTION_TEST)
	java -ea -cp $(BUILD_DIR)/java-test-classes org.ckarta.servlet.CkartaAsyncContextTest
	java -ea -cp $(BUILD_DIR)/java-test-classes:$(JAKARTA_SERVLET_API_JAR) org.ckarta.servlet.CkartaServletAsyncContextTest
	java -ea -cp $(BUILD_DIR)/java-test-classes:$(JAKARTA_SERVLET_API_JAR) org.ckarta.servlet.CkartaServletRequestAsyncTest
	$(ABI_TEST)
	$(CONFIG_TEST) tests/config/valid.conf
	$(ERROR_TEST)
	$(ERROR_RACE_TEST)
	$(TERMINAL_RACE_TEST)
	$(COMPLETION_QUEUE_TEST)
	$(CONNECTION_TEST)
	! $(CONFIG_TEST) tests/config/invalid.conf
	./tests/smoke_bootstrap.sh $(TARGET)
