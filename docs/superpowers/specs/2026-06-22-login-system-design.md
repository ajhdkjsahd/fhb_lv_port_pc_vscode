# 智能水产养殖中控 —— 登录/注册系统设计

- 日期：2026-06-22
- 平台：LVGL v9 PC port（SDL），屏幕 800×480，深色蓝/红主题
- 目标：实现登录页、注册页、空白主屏，含微信式预览键盘、2 秒自消弹窗、FreeType 中文字体、FA6 图标，并预留登录/注册验证回调接口供用户填写实际逻辑。

## 1. 决策记录

| 项 | 决策 |
|---|---|
| 登录成功后跳转 | 预留一个空白主屏（带「退出登录」按钮），后续再加功能 |
| 密码键盘预览行内容 | 明文显示 |
| 启动首屏 | 登录页作为启动首屏（温度页不再作启动页，代码保留） |
| FreeType 中文字号 | 16 / 20 / 24 / 26 |
| FA6 图标字号 | 24 |
| 键盘预览行实现 | 复合控件（预览 label + 标准 lv_keyboard），不侵入 LVGL 内核 |
| 页面导航 | 一屏一 page，用 lv_screen_load 切换 |
| 提示弹窗 | 自绘小型模态弹窗（2s 自消 + 点屏即消），不用 lv_msgbox |

## 2. 文件结构（新增）

```
src/ui/
├── fonts/
│   ├── app_fonts.h           # 新增 FreeType 字体声明 + init/deinit
│   └── freetype_fonts.c      # 新增：lv_freetype_font_create 加载 SIMKAI + FA6
├── widgets/
│   └── preview_keyboard.h/.c # 新增：微信式预览键盘复合控件
└── pages/
    ├── login_page.h/.c       # 新增：登录页
    ├── register_page.h/.c    # 新增：注册页
    ├── main_page.h/.c        # 新增：空白主屏（带退出登录）
    └── ui_popup.h/.c         # 新增：2s 自消 / 点屏即消的小弹窗
```

CMakeLists.txt 已 glob `src/ui/pages/*.c` 与 `src/ui/fonts/*.c`；需新增 glob `src/ui/widgets/*.c`。

## 3. FreeType 集成

### 3.1 配置改动（lv_conf.h）

- `LV_USE_FREETYPE 1`（当前为 0）
- `LV_USE_TINY_TTF` 保留 1（与 FreeType 不冲突，保留现有预生成字体可用）
- `LV_FREETYPE_USE_TINY_FMT 0`（使用 outline 轮廓渲染，中文清晰）
- `LV_FREETYPE_CACHE_FT_GLYPH_CNT` 维持 256

### 3.2 运行时字体加载

不再依赖预生成 `.c`，运行时从 TTF/OTF 加载：

- SIMKAI.TTF：字号 16 / 20 / 24 / 26（中文正文/标题）
- FA6-Free-Solid-900.otf：字号 24（账号 `user`、密码 `lock` 图标；未找到合适水产 logo 时复用 FA6）

通过 `lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_STYLE_OUTLINE, size, ...)` 创建。

### 3.3 资源路径

- TTF/OTF 位于 `src/ui/fonts/`，工程内已有 `SIMKAI.TTF`、`FA6-Free-Solid-900.otf`。
- 路径以 LVGL 文件系统 `A:` 虚拟盘或运行时绝对路径访问；实现时按 pc port 实际可用方式确认（若 `A:` 未配置则用相对可执行文件路径或绝对路径）。

### 3.4 生命周期

- `app_fonts_init()`：main 启动时调用一次，创建并缓存全部字体句柄。
- `app_fonts_deinit()`：退出时释放（pc demo 可不强制，但有就调）。

### 3.5 构建依赖

- FreeType 库经 pkg-config 可用（已确认本机 freetype2 26.4.20）。
- CMakeLists.txt 中 LVGL 子目录已 `option(LV_USE_FREETYPE ...)`；启用后 LVGL 会条件链接 freetype。需确保顶层 CMake 能 `find_package(Freetype)` 或经 pkg-config 链接（实现时确认 LVGL 子模块是否自动处理）。

## 4. 页面导航

- 每页一个独立 `lv_screen`：`login_screen`、`register_screen`、`main_screen`。
- 切换用 `lv_screen_load(target)`。
- 启动时 `login_screen` 为首屏。

## 5. 登录页 login_page（800×480，深色蓝/红主题）

### 5.1 布局

```
┌──────────────────────────────────────┐
│            智能水产养殖系统            │  标题 24
│        （顶部 logo 图标 + 标题）       │
│                                      │
│   👤  ┌────────────────────────┐     │  账号输入框（FA6 user 图标 + textarea）
│       └────────────────────────┘     │
│   🔒  ┌────────────────────────┐     │  密码输入框（FA6 lock 图标 + textarea 密码模式）
│       └────────────────────────┘     │
│        ┌──────────┐  ┌──────────┐    │
│        │   登录   │  │   注册   │    │  两个按钮
│        └──────────┘  └──────────┘    │
└──────────────────────────────────────┘
```

- 顶部 logo 图标：无合适水产 logo 时用 FA6 图标（实现时选一个合适的，如 water/fish 类）。
- 输入框前置图标：FA6 `user`（账号）、`lock`（密码）。
- 密码框 `lv_textarea_set_password_mode(ta, true)`。

### 5.2 输入交互

- 点击输入框 → `LV_EVENT_FOCUSED` → 弹出预览键盘并绑定该 textarea（见 §7）。
- 点击空白 → 预览键盘收起、textarea 失焦。

### 5.3 按钮行为

- 「登录」按钮：
  1. 读取账号、密码文本。
  2. 调用预留接口 `login_auth_cb(user, pass)`（见 §9）。
  3. 返回 true → `ui_popup_show("登录成功", true)` → 弹窗关闭后 `lv_screen_load(main_screen)`。
  4. 返回 false → `ui_popup_show("账号或密码错误", false)`。
- 「注册」按钮：`lv_screen_load(register_screen)`。

### 5.4 空输入处理

- 账号或密码为空时点登录：直接 `ui_popup_show("请输入账号和密码", false)`，不调用回调。

## 6. 注册页 register_page

### 6.1 布局

```
┌──────────────────────────────────────┐
│                注册账号               │
│   👤  [ 账号            ]             │
│   🔒  [ 密码            ]             │
│   🔒  [ 确认密码        ]             │
│        ┌──────────┐  ┌──────────┐    │
│        │   注册   │  │   返回   │    │
│        └──────────┘  └──────────┘    │
└──────────────────────────────────────┘
```

### 6.2 按钮行为

- 「注册」按钮：
  1. 校验：三个字段非空、两次密码一致；不满足 → `ui_popup_show` 提示具体原因，不调用回调。
  2. 调用预留接口 `register_commit_cb(user, pass)`（见 §9）。
  3. 返回 true → `ui_popup_show("注册成功", true)` → 关闭后 `lv_screen_load(login_screen)`。
  4. 返回 false → `ui_popup_show("注册失败", false)`（如账号已存在等）。
- 「返回」按钮：`lv_screen_load(login_screen)`。

### 6.3 输入交互

- 与登录页一致：点输入框弹预览键盘，点空白收起。

## 7. 预览键盘控件 preview_keyboard

微信聊天框风格：键盘最上方多一行实时镜像输入内容，空则不显示。

### 7.1 API

```c
void preview_keyboard_init(void);              // 在屏幕底部创建隐藏容器，全局单例
void preview_keyboard_attach(lv_obj_t * ta);   // 绑定目标 textarea，显示键盘
void preview_keyboard_hide(void);              // 收起键盘，解绑 textarea
```

### 7.2 结构

```
┌─────────────────────────┐
│  预览行 label（空则隐藏） │  ← 实时镜像 ta 文本
├─────────────────────────┤
│  lv_keyboard（标准键盘）  │
└─────────────────────────┘
```

- 容器置于 active screen 底部，默认 `LV_OBJ_FLAG_HIDDEN`。
- 预览行：`lv_label`，靠左对齐；高度自适应，空内容时 `lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN)`，有内容时 remove。
- 内部 `lv_keyboard`：`lv_keyboard_set_textarea(kb, ta)` 绑定当前 textarea。

### 7.3 预览行实时镜像

- 监听目标 textarea 的 `LV_EVENT_VALUE_CHANGED`。
- 回调内 `lv_textarea_get_text(ta)` → 写入预览 label。
- 为空 → 隐藏预览行；非空 → 显示预览行。
- 按需求：密码框预览行也**明文显示**。

### 7.4 弹出 / 收起逻辑

- textarea `LV_EVENT_FOCUSED` → `preview_keyboard_attach(ta)` + `lv_keyboard_set_textarea(kb, ta)`。
- 全屏点击捕获：在 active screen 注册 `LV_EVENT_CLICKED`；若点击目标既非 textarea 也非键盘/预览容器 → `preview_keyboard_hide()` + `lv_keyboard_set_textarea(kb, NULL)`。
- 注意：屏幕切换时需保证预览键盘跟随当前 screen 或重新挂载（实现时按 LVGL v9 screen 切换机制确认键盘归属）。

## 8. 弹窗控件 ui_popup

### 8.1 API

```c
void ui_popup_show(const char * msg, bool success);  // success 决定图标与配色
void ui_popup_close(void);
```

### 8.2 结构

- 全屏半透明遮罩 `lv_obj`（`bg_opa` 半透明、radius 0、覆盖全屏）。
- 居中圆角小卡片：图标（FA6 ✓ 勾 / ✗ 叉，或 `success` 用对勾、`false` 用叉）+ 文案 label。
- `success=true`：绿色/主题正向色 + 对勾图标。
- `success=false`：红色/主题负向色 + 叉图标。

### 8.3 行为

- 显示时挂 `lv_timer`，2 秒后 `ui_popup_close()`。
- 遮罩 `LV_EVENT_CLICKED` → 立即 `ui_popup_close()` 并 `lv_timer_del`。
- 同一时刻只保留一个弹窗（再 show 前先 close 已有）。

## 9. 空白主屏 main_page

- 顶部标题「主界面（待开发）」。
- 右上「退出登录」按钮 → `lv_screen_load(login_screen)`。
- 主体留空，后续接水产养殖功能模块。

## 10. 启动流程改动（main.c）

1. 初始化 SDL HAL、LVGL、显示（800×480）、主题（保持现有深色蓝/红）。
2. `app_fonts_init()`。
3. `ui_popup` 无需主动 init（按需 show）。
4. `preview_keyboard_init()`。
5. `login_page_init()` → `lv_screen_load(login_screen)`。
6. 主循环：`lv_timer_handler()` + sleep（保持现有）。
7. 现有温度页加载移除/保留：温度页不再作启动页；代码保留以便后续接入。

## 11. 回调接口（用户填写实际验证逻辑）

### 11.1 login_page.h

```c
typedef bool (*login_auth_cb_t)(const char * user, const char * pass);  // true=验证通过
void login_page_set_auth_cb(login_auth_cb_t cb);
```

### 11.2 register_page.h

```c
typedef bool (*register_commit_cb_t)(const char * user, const char * pass);  // true=注册成功
void register_page_set_commit_cb(register_commit_cb_t cb);
```

### 11.3 用法

用户在 `main.c` 或单独 `auth.c` 实现这两个函数（真实校验/存储逻辑），通过 setter 注入。UI 仅负责调用并据返回值弹窗，不关心验证细节。若未注入回调，UI 默认行为：登录/注册视为失败并提示「未配置验证逻辑」（避免误判成功）。

## 12. 字号用途

| 字体 | 字号 | 用途 |
|---|---|---|
| SIMKAI | 16 | 小字/辅助说明 |
| SIMKAI | 20 | 输入框文本、按钮文本 |
| SIMKAI | 24 | 页面标题 |
| SIMKAI | 26 | （预留，登录页主标题或主屏标题） |
| FA6 | 24 | 输入框前置图标、弹窗图标 |

## 13. 范围与非目标

- 本期只做登录/注册/空白主屏 + 预览键盘 + 弹窗 + FreeType 字体 + FA6 图标。
- 不做：实际账号存储/校验逻辑（用户填）、水产养殖业务功能、温度页改动（仅移出启动链）、网络通信。
- 不做：账号找回、记住密码、自动登录（YAGNI）。

## 14. 风险与待确认

- FreeType 路径：`A:` 虚拟盘是否在 pc port 已配；若未配则用绝对/相对路径。实现时确认。
- CMake 中 FreeType 链接：LVGL 子模块开启 `LV_USE_FREETYPE` 后是否自动 `find_package(Freetype)`；若否，需在顶层 CMake 加 `find_package(Freetype REQUIRED)` 并 `target_link_libraries`。实现时确认。
- 预览键盘与多 screen 归属：键盘是单例挂当前 screen 还是每 screen 重建，按 LVGL v9 实测确认。
