# Makefile — GEC6818 Ollama 客户端
# 
# 交叉编译 (ARM Cortex-A53, GEC6818):
#   make
#
# 本地编译 (PC x86_64, 用于测试):
#   make local
#
# 清理:
#   make clean

# ===== ARM 交叉编译配置 (GEC6818 / S5P6818) =====
CROSS_COMPILE = /usr/local/arm/5.4.0/bin/arm-linux-
CC_ARM        = $(CROSS_COMPILE)gcc
STRIP_ARM     = $(CROSS_COMPILE)strip

# ===== 本地编译配置 (x86_64 测试) =====
CC_LOCAL      = gcc

# ===== 编译选项 =====
CFLAGS   = -Wall -Wextra -O2 -std=c99 -D_GNU_SOURCE
LDFLAGS  = -s

TARGET   = ollama_client
SRCS     = main.c http_client.c
OBJS     = main.o http_client.o

# ===== 默认目标：ARM 交叉编译 =====
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC_ARM) $(LDFLAGS) -o $@ $^
	$(STRIP_ARM) $@
	@echo "=== Build complete: $(TARGET) (ARM) ==="

%.o: %.c
	$(CC_ARM) $(CFLAGS) -c -o $@ $<

# ===== 本地编译（PC 端测试） =====
local:
	$(CC_LOCAL) $(CFLAGS) -o $(TARGET) $(SRCS)
	@echo "=== Build complete: $(TARGET) (local x86_64) ==="

# ===== 清理 =====
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all local clean
