CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -g -O0
CPPFLAGS := -Iinclude -Ithird_party/cjson
LDLIBS := -lpthread -lcurl

BIN := maskd

SRCS := \
	src/main.c \
	src/common.c \
	src/config.c \
	src/ring_buffer.c \
	src/reactor.c \
	src/sandbox.c \
	src/tool_gateway.c \
	src/llm_client.c \
	src/ipc.c \
	src/event_export.c \
	src/tools/tool_sysinfo.c \
	src/tools/tool_shell.c \
	src/tools/tool_net.c \
	src/tools/tool_asset.c \
	src/tools/tool_action.c \
	third_party/cjson/cJSON.c

OBJS := $(SRCS:.c=.o)

TEST_BIN := tests/smoke_tools

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/smoke_tools.c src/common.c src/tool_gateway.c src/sandbox.c src/ring_buffer.c \
             src/tools/tool_sysinfo.c src/tools/tool_shell.c third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN) $(OBJS) $(TEST_BIN)
