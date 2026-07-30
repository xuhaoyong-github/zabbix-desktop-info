# Zabbix Desktop Info

C + Win32 + GDI+ 实现的 Zabbix 桌面实时监控仪表盘程序。登录 Zabbix 后选择设备监控项，在桌面以透明窗口显示实时数据。

## 功能

- **三种图形样式**
  - **仪表盘** (Gauge)：圆形指针仪表盘，支持配置警告/严重阈值变色（绿→黄→红）
  - **卡片** (Card)：圆角矩形显示标题和数值
  - **趋势图** (Trend)：折线图展示历史趋势数据

- **桌面透明窗口**：使用 `UpdateLayeredWindow` + 32位 ARGB DIB，支持逐像素透明
- **窗口置顶**：可设置 always-on-top，悬浮在所有窗口之上
- **拖拽移动**：左键拖拽移动 widget 位置，位置自动保存
- **点击穿透**：透明区域自动穿透点击到桌面
- **系统托盘**：右键托盘图标可添加 widget、修改登录设置、刷新所有、退出
- **配置持久化**：所有配置（Zabbix 连接信息、widget 配置）保存到 `%APPDATA%\ZabbixDesktopInfo\config.json`
- **GUI 选择监控项**：主机列表 → 监控项列表（带搜索）→ 选择 widget 类型

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C (C99) + C++ (GDI+ 渲染层) |
| 窗口 | Win32 API (CreateWindowEx, UpdateLayeredWindow) |
| 图形 | GDI+ (Anti-aliased rendering) |
| HTTP | WinINet (支持 HTTP/HTTPS) |
| JSON | 自实现轻量级 JSON 解析器 |
| 透明窗口 | WS_EX_LAYERED + UpdateLayeredWindow + premultiplied ARGB |

## 构建

### 依赖
- MinGW GCC 6.3+ (或 MSVC / Clang)
- Windows SDK (GDI+, WinINet, Common Controls)

### 使用 build.bat (MinGW)
```bat
cd D:\Code\zabbix-desktop-info
build.bat
```

### 使用 CMake
```bat
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

### 手动编译
```bash
gcc -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600 -Isrc -O2 -Wall -c src/json.c -o build/json.o
gcc ... -c src/zabbix_api.c -o build/zabbix_api.o
gcc ... -c src/config.c -o build/config.o
g++ ... -c src/render.cpp -o build/render.o
gcc ... -c src/widget.c -o build/widget.o
gcc ... -c src/ui_login.c -o build/ui_login.o
gcc ... -c src/ui_select.c -o build/ui_select.o
gcc ... -c src/main.c -o build/main.o
g++ -o zabbix-desktop.exe build/*.o -lgdiplus -lwininet -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -lole32 -luuid -mwindows
```

## 使用方法

1. 运行 `zabbix-desktop.exe`
2. 首次运行会弹出登录对话框，填入 Zabbix API URL、用户名、密码
   - URL 格式：`http://your-zabbix-server/zabbix` 或 `https://...`
   - 程序会自动拼接 `/api_jsonrpc.php`
3. 登录成功后，右键系统托盘图标 → **"Add Widget..."**
4. 在弹出的选择窗口中：
   - 左侧选择主机
   - 右侧选择监控项（可用搜索框过滤）
   - 选择 Widget 类型（Gauge / Card / Trend）
   - 点击 **"Add Widget"**
5. Widget 出现在桌面上，自动开始定时刷新数据
6. 右键 Widget 可：
   - **Always on Top**：切换置顶
   - **Configure...**：配置刷新间隔、透明度、颜色、阈值等
   - **Refresh Now**：立即刷新
   - **Remove Widget**：删除该 widget

## 项目结构

```
zabbix-desktop-info/
├── src/
│   ├── json.h / json.c          # 轻量级 JSON 解析器
│   ├── zabbix_api.h / .c        # Zabbix JSON-RPC API 客户端 (WinINet)
│   ├── config.h / .c            # 配置文件读写 (JSON 格式)
│   ├── render.h / .cpp          # GDI+ 渲染：仪表盘/卡片/趋势图
│   ├── widget.h / .c           # Widget 透明窗口管理 (UpdateLayeredWindow)
│   ├── ui_login.h / .c          # 登录对话框
│   ├── ui_select.h / .c         # 主机/监控项选择对话框
│   └── main.c                   # 入口：系统托盘 + 消息循环
├── CMakeLists.txt
├── build.bat
└── README.md
```

## Zabbix API 调用

| API 方法 | 用途 |
|----------|------|
| `user.login` | 登录获取 auth token（兼容 5.4+ 的 username 字段） |
| `host.get` | 获取主机列表 |
| `item.get` | 获取主机监控项（含最新值） |
| `history.get` | 获取历史趋势数据 |

## 配置文件

路径：`%APPDATA%\ZabbixDesktopInfo\config.json`

```json
{
  "zabbix_url": "http://192.168.1.100/zabbix",
  "zabbix_user": "Admin",
  "zabbix_pass": "zabbix",
  "widgets": [
    {
      "type": 0,
      "item_id": "12345",
      "host_name": "Web Server",
      "item_name": "CPU utilization",
      "units": "%",
      "x": 100, "y": 100,
      "width": 200, "height": 220,
      "always_on_top": 1,
      "refresh_interval": 30,
      "gauge_min": 0, "gauge_max": 100,
      "gauge_warn": 70, "gauge_crit": 90,
      "gauge_warn_enabled": 1, "gauge_crit_enabled": 1,
      "bg_opacity": 200, "accent_color": 4820117
    }
  ]
}
```
