CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -g -O0
CPPFLAGS := -Iinclude -Ithird_party/cjson -Ithird_party
LDLIBS := -lpthread -lcurl

# Enable SQLite support by defining USE_SQLITE
# To build with SQLite: make USE_SQLITE=1
ifdef USE_SQLITE
	CPPFLAGS += -DUSE_SQLITE
	LDLIBS += -ldl -lm
	SRCS += third_party/sqlite3.c
else
	CPPFLAGS += -Ithird_party
endif

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
	src/storage.c \
	src/rag.c \
	src/tools/tool_sysinfo.c \
	src/tools/tool_shell.c \
	src/tools/tool_net.c \
	src/tools/tool_asset.c \
	src/tools/tool_action.c \
	src/tools/tool_file_ops.c \
	src/tools/tool_proc_mgr.c \
	src/tools/tool_net_check.c \
	third_party/cjson/cJSON.c

OBJS := $(SRCS:.c=.o)

TEST_BIN := tests/smoke_tools
TEST_NEW_BIN := tests/smoke_new_features

.PHONY: all clean run test test-new test-storage

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

test-new: $(TEST_NEW_BIN)
	./$(TEST_NEW_BIN)

$(TEST_BIN): tests/smoke_tools.c src/common.c src/tool_gateway.c src/sandbox.c src/ring_buffer.c \
             src/tools/tool_sysinfo.c src/tools/tool_shell.c third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

$(TEST_NEW_BIN): tests/smoke_new_features.c src/common.c src/tool_gateway.c src/llm_client.c src/rag.c third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN) $(OBJS) $(TEST_BIN) mask_storage.db
