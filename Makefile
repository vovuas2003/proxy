BUILD_DIR = build

PYTHON    = python3
CC_ROUTER = ~/x-tools/mipsel-unknown-linux-uclibc/bin/mipsel-unknown-linux-uclibc-gcc
CC_NATIVE = gcc
GO_BUILD  = go build

ROUTER_EXEC    = $(BUILD_DIR)/c_linux_fork_router
NATIVE_EXEC    = $(BUILD_DIR)/c_linux_pthread_native
GO_NATIVE_EXEC = $(BUILD_DIR)/go_proxy_native
GO_WIN_EXEC    = $(BUILD_DIR)/go_proxy_windows_amd64.exe
GO_LINUX_EXEC  = $(BUILD_DIR)/go_proxy_linux_amd64
GO_ARM_EXEC    = $(BUILD_DIR)/go_proxy_linux_arm64

.DEFAULT_GOAL := help

help: ## Show list of all targets (default)
	@echo "Available targets in Makefile:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "%-15s %s\n", $$1, $$2}'

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(ROUTER_EXEC): c_linux_fork.c | $(BUILD_DIR)
	$(CC_ROUTER) -Wall -Wextra -DDAEMON -DBUFFER_SIZE=512 c_linux_fork.c -o $(ROUTER_EXEC)

$(NATIVE_EXEC): c_linux_pthread.c | $(BUILD_DIR)
	$(CC_NATIVE) -Wall -Wextra -DDEBUG c_linux_pthread.c -lpthread -o $(NATIVE_EXEC)

$(GO_NATIVE_EXEC): go_proxy.go | $(BUILD_DIR)
	$(GO_BUILD) -o $(GO_NATIVE_EXEC) go_proxy.go

$(GO_WIN_EXEC): go_proxy.go | $(BUILD_DIR)
	GOOS=windows GOARCH=amd64 $(GO_BUILD) -o $(GO_WIN_EXEC) go_proxy.go

$(GO_LINUX_EXEC): go_proxy.go | $(BUILD_DIR)
	GOOS=linux GOARCH=amd64 $(GO_BUILD) -o $(GO_LINUX_EXEC) go_proxy.go

$(GO_ARM_EXEC): go_proxy.go | $(BUILD_DIR)
	GOOS=linux GOARCH=arm64 $(GO_BUILD) -o $(GO_ARM_EXEC) go_proxy.go

all: native router go_all ## Build all (native, router, go_all)

all_release: native router go_cross ## Build all for release (native, router, go_cross)

run: ## Run python_asyncio.py on http://127.0.0.1:8080
	$(PYTHON) python_asyncio.py 127.0.0.1 8080

router: $(ROUTER_EXEC) ## Build c_linux_fork.c for router MIPS le 32-bit uClibc

native: $(NATIVE_EXEC) ## Native build c_linux_pthread.c

go: $(GO_NATIVE_EXEC) ## Native build go_proxy.go

go_cross: $(GO_WIN_EXEC) $(GO_LINUX_EXEC) $(GO_ARM_EXEC) ## Build go_proxy.go for x86-64 Windows/Linux and Linux arm64

go_all: go go_cross ## Native and cross go_proxy.go build (go, go_cross)

clean: ## Delete build directory
	@rm -rf $(BUILD_DIR)

.PHONY: help all all_release run router native go go_cross go_all clean
