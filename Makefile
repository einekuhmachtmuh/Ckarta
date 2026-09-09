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
CONNECTION_REGISTRY_TEST := $(BIN_DIR)/ckarta-connection-registry-test
EVENT_LOOP_TEST := $(BIN_DIR)/ckarta-event-loop-test
TCP_EVENT_INTEGRATION_TEST := $(BIN_DIR)/ckarta-tcp-event-integration-test
HTTP_PARSER_TEST := $(BIN_DIR)/ckarta-http-parser-test
HTTP_CHUNKED_TEST := $(BIN_DIR)/ckarta-http-chunked-test
HTTP_INPUT_TEST := $(BIN_DIR)/ckarta-http-input-test
HTTP_CONNECTION_READER_TEST := $(BIN_DIR)/ckarta-http-connection-reader-test
HTTP_RESPONSE_TEST := $(BIN_DIR)/ckarta-http-response-test
NATIVE_ASYNC_BRIDGE_TEST := $(BIN_DIR)/ckarta-native-async-bridge-smoke
JAVA_NATIVE_ASYNC_BRIDGE_TEST := $(BIN_DIR)/ckarta-native-async-bridge-test

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

$(TARGET): c/core/main.c c/config/ck_config.c c/config/ck_config.h c/error/ck_error.c c/error/ck_error.h c/completion/ck_completion_queue.c c/completion/ck_completion_queue.h c/event/ck_completion_notification.c c/event/ck_completion_notification.h c/event/ck_event_loop.c c/event/ck_event_loop.h c/connection/ck_connection.c c/connection/ck_connection.h c/jni/ck_jni_runtime.c c/jni/ck_jni_runtime.h c/jni/ck_request.c c/jni/ck_request.h c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h classes
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) c/core/main.c c/config/ck_config.c c/error/ck_error.c c/completion/ck_completion_queue.c c/event/ck_event_loop.c c/event/ck_completion_notification.c c/connection/ck_connection.c c/jni/ck_jni_runtime.c c/jni/ck_request.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@ $(LDFLAGS)

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

$(CONNECTION_TEST): tests/connection/ck_connection_test.c c/connection/ck_connection.c c/connection/ck_connection.h c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/connection/ck_connection_test.c c/connection/ck_connection.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@

$(CONNECTION_REGISTRY_TEST): tests/connection/ck_connection_registry_test.c c/connection/ck_connection_registry.c c/connection/ck_connection_registry.h c/connection/ck_connection.c c/connection/ck_connection.h c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/connection/ck_connection_registry_test.c c/connection/ck_connection_registry.c c/connection/ck_connection.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@

$(EVENT_LOOP_TEST): tests/event/ck_event_loop_test.c c/event/ck_event_loop.c c/event/ck_event_loop.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/event/ck_event_loop_test.c c/event/ck_event_loop.c -o $@

$(TCP_EVENT_INTEGRATION_TEST): tests/net/ck_tcp_event_integration_test.c c/net/ck_tcp_listener.c c/net/ck_tcp_listener.h c/event/ck_event_loop.c c/event/ck_event_loop.h c/connection/ck_connection_registry.c c/connection/ck_connection_registry.h c/connection/ck_connection.c c/connection/ck_connection.h c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/net/ck_tcp_event_integration_test.c c/net/ck_tcp_listener.c c/event/ck_event_loop.c c/connection/ck_connection_registry.c c/connection/ck_connection.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@

$(HTTP_PARSER_TEST): tests/http/ck_http_parser_test.c c/http/ck_http_parser.c c/http/ck_http_parser.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/http/ck_http_parser_test.c c/http/ck_http_parser.c -o $@

$(HTTP_CHUNKED_TEST): tests/http/ck_http_chunked_test.c c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/http/ck_http_chunked_test.c c/http/ck_http_chunked.c -o $@

$(HTTP_INPUT_TEST): tests/http/ck_http_input_test.c c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/http/ck_http_input_test.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@

$(HTTP_CONNECTION_READER_TEST): tests/http/ck_http_connection_reader_test.c c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/http/ck_http_connection_reader_test.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@

$(HTTP_RESPONSE_TEST): tests/output/ck_http_response_test.c c/output/ck_http_response.c c/output/ck_http_response.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/output/ck_http_response_test.c c/output/ck_http_response.c -o $@

$(JAVA_NATIVE_ASYNC_BRIDGE_TEST): tests/java/CkartaNativeAsyncBridgeTest.java java/org/ckarta/servlet/CkartaNativeAsyncBridge.java java/org/ckarta/servlet/CkartaServletRequestAdapter.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java $(JAKARTA_SERVLET_API_JAR)
	@mkdir -p $(BUILD_DIR)/java-test-classes
	javac --release 21 -cp $(JAKARTA_SERVLET_API_JAR) -d $(BUILD_DIR)/java-test-classes tests/java/CkartaNativeAsyncBridgeTest.java java/org/ckarta/servlet/CkartaNativeAsyncBridge.java java/org/ckarta/servlet/CkartaServletRequestAdapter.java java/org/ckarta/servlet/CkartaServletAsyncContext.java java/org/ckarta/servlet/CkartaAsyncContext.java java/org/ckarta/servlet/CkartaAsyncCycleBinding.java

$(NATIVE_ASYNC_BRIDGE_TEST): tests/native_async_bridge_smoke.c $(JAVA_NATIVE_ASYNC_BRIDGE_TEST) c/jni/ck_async_bridge.c c/jni/ck_async_bridge.h c/connection/ck_connection_registry.c c/connection/ck_connection_registry.h c/connection/ck_connection.c c/connection/ck_connection.h c/http/ck_http_connection_reader.c c/http/ck_http_connection_reader.h c/http/ck_http_input.c c/http/ck_http_input.h c/http/ck_http_parser.c c/http/ck_http_parser.h c/http/ck_http_chunked.c c/http/ck_http_chunked.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/native_async_bridge_smoke.c c/jni/ck_async_bridge.c c/connection/ck_connection_registry.c c/connection/ck_connection.c c/http/ck_http_connection_reader.c c/http/ck_http_input.c c/http/ck_http_parser.c c/http/ck_http_chunked.c -o $@ $(LDFLAGS)

$(CONFIG_TEST): tests/config_load_test.c c/config/ck_config.c c/config/ck_config.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) tests/config_load_test.c c/config/ck_config.c -o $@

clean:
	rm -rf $(BUILD_DIR)

test: all $(JAVA_ASYNC_TEST) $(JAVA_SERVLET_API_TEST) $(JAVA_SERVLET_REQUEST_ASYNC_TEST) $(JAVA_NATIVE_ASYNC_BRIDGE_TEST) $(ABI_TEST) $(CONFIG_TEST) $(ERROR_TEST) $(ERROR_RACE_TEST) $(TERMINAL_RACE_TEST) $(COMPLETION_QUEUE_TEST) $(CONNECTION_TEST) $(CONNECTION_REGISTRY_TEST) $(EVENT_LOOP_TEST) $(TCP_EVENT_INTEGRATION_TEST) $(HTTP_PARSER_TEST) $(HTTP_CHUNKED_TEST) $(HTTP_INPUT_TEST) $(HTTP_CONNECTION_READER_TEST) $(HTTP_RESPONSE_TEST) $(NATIVE_ASYNC_BRIDGE_TEST)
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
	$(CONNECTION_REGISTRY_TEST)
	$(EVENT_LOOP_TEST)
	$(TCP_EVENT_INTEGRATION_TEST)
	$(HTTP_PARSER_TEST)
	$(HTTP_CHUNKED_TEST)
	$(HTTP_INPUT_TEST)
	$(HTTP_CONNECTION_READER_TEST)
	$(HTTP_RESPONSE_TEST)
	$(NATIVE_ASYNC_BRIDGE_TEST)
	! $(CONFIG_TEST) tests/config/invalid.conf
	./tests/smoke_bootstrap.sh $(TARGET)
