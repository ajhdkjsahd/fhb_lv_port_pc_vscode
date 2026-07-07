# ============================================================
#  ai-core Makefile
# ============================================================
# 用法:
#   make             # 默认 = make pc_test
#   make pc_test     # 编译 PC 测试版 (硬件 mock + printf)
#   make run         # 编译并运行 PC 测试
#   make arm         # 交叉编译 ARM 板子版 (真实硬件)
#   make get_cjson   # 下载 cJSON 单文件库
#   make clean       # 清理
# ------------------------------------------------------------

# ── 工具链 ──
PC_CC   := gcc
ARM_CC  := arm-linux-gcc

# ── 编译选项 ──
CFLAGS  := -Wall -Wextra -O2 -I.
LDFLAGS := -lpthread
PC_DEFINES := -D__PC_TEST__

# ── 源文件 ──
COMMON_SRCS := ai_agent.c ai_tools.c http_client.c cJSON.c
PC_SRCS     := main_test.c $(COMMON_SRCS)

# ── 输出 ──
PC_BIN := agent_test
ARM_BIN := ai_core.a

# ============================================================
# 默认目标
# ============================================================
.PHONY: all pc_test arm run get_cjson clean help

all: pc_test

# ── PC 测试版 (WSL2 Ubuntu 上跑) ──
pc_test: $(PC_BIN)

$(PC_BIN): $(PC_SRCS) cJSON.h
	$(PC_CC) $(CFLAGS) $(PC_DEFINES) $^ -o $@ $(LDFLAGS)
	@echo ""
	@echo "✓ PC 测试版编译完成: ./$(PC_BIN)"
	@echo "  运行前改 main_test.c 顶部的 OLLAMA_HOST 为你的 Windows IP"
	@echo "  然后执行: make run"
	@echo ""

# ── 直接跑 PC 测试 ──
run: $(PC_BIN)
	./$(PC_BIN)

# ── ARM 板子交叉编译版 (生成静态库, 给 LVGL 项目链接用) ──
arm: $(ARM_BIN)

$(ARM_BIN): ai_agent.c ai_tools.c http_client.c cJSON.h
	$(ARM_CC) $(CFLAGS) -c ai_agent.c   -o ai_agent.o
	$(ARM_CC) $(CFLAGS) -c ai_tools.c   -o ai_tools.o
	$(ARM_CC) $(CFLAGS) -c http_client.c -o http_client.o
	ar rcs $@ ai_agent.o ai_tools.o http_client.o
	@echo ""
	@echo "✓ ARM 静态库生成: $(ARM_BIN)"
	@echo "  在 LVGL 工程里链接: -L./ai-core -lai_core"
	@echo "  再加 cJSON.c 编译进 LVGL 工程即可"
	@echo ""

# ── 下载 cJSON 单文件库 ──
get_cjson:
	@if [ -f cJSON.c ] && [ -f cJSON.h ]; then \
		echo "cJSON 已存在, 跳过下载"; \
	else \
		echo "下载 cJSON..."; \
		wget -O cJSON.c https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c; \
		wget -O cJSON.h https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h; \
		echo "✓ cJSON 已就绪"; \
	fi

# ── 清理 ──
clean:
	rm -f $(PC_BIN) $(ARM_BIN) *.o
	@echo "已清理"

# ── 帮助 ──
help:
	@echo "ai-core Makefile 命令:"
	@echo "  make             编译 PC 测试版 (默认)"
	@echo "  make run         编译并运行 PC 测试"
	@echo "  make arm         交叉编译 ARM 静态库 (ai_core.a)"
	@echo "  make get_cjson   下载 cJSON 单文件库"
	@echo "  make clean       清理产物"
	@echo "  make help        显示本帮助"
