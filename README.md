# Zabbix Desktop Info

[中文](#中文) | [English](#english)

---

<a id="中文"></a>

## 中文

### 简介

C + Win32 + GDI+ 实现的 Zabbix 桌面实时监控仪表盘程序。登录 Zabbix 后选择设备监控项，在桌面以透明窗口显示实时数据。

### 功能

- **三种图形样式**
  - **仪表盘** (Gauge)：圆形指针仪表盘，支持配置警告/严重阈值变色（绿/黄/红）
  - **卡片** (Card)：圆角矩形显示标题和数值
  - **趋势图** (Trend)：折线图展示历史趋势数据

- **桌面透明窗口**：使用 `UpdateLayeredWindow` + 32位 ARGB DIB，支持逐像素透明
- **窗口置顶**：可设置 always-on-top，悬浮在所有窗口之上
- **Win+D 防最小化**：组件窗口在 Win+D 时保持显示，不会被最小化隐藏
- **拖拽移动**：左键拖拽移动 widget 位置，位置自动保存
- **点击穿透**：透明区域自动穿透点击到桌面
- **系统托盘**：右键托盘图标可添加 widget、修改登录设置、刷新所有、退出
- **配置持久化**：所有配置保存到 `%APPDATA%\ZabbixDesktopInfo\config.json`
- **密码加密存储**：使用 Windows DPAPI 加密密码，配置文件中不存明文
- **GUI 选择监控项**：主机列表（带搜索）→ 监控项列表（带搜索）→ 选择 widget 类型
- **HTTPS 支持**：支持 HTTP 和 HTTPS，兼容自签名证书

### 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C (C99) + C++ (GDI+ 渲染层) |
| 窗口 | Win32 API (CreateWindowEx, UpdateLayeredWindow) |
| 图形 | GDI+ (抗锯齿渲染) |
| HTTP | WinINet (支持 HTTP/HTTPS) |
| JSON | 自实现轻量级 JSON 解析器 |
| 密码保护 | Windows DPAPI (CryptProtectData / CryptUnprotectData) |
| 透明窗口 | WS_EX_LAYERED + UpdateLayeredWindow + premultiplied ARGB |

### 构建

#### 依赖
- MinGW GCC 6.3+（或 MSVC / Clang）
- Windows SDK（GDI+, WinINet, Common Controls, Crypt32）

#### 使用 build.bat (MinGW)
```bat
cd D:\Code\zabbix-desktop-info
build.bat
```

#### 使用 CMake
```bat
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

#### 手动编译
```bash
gcc -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600 -Isrc -O2 -Wall -c src/json.c -o build/json.o
gcc ... -c src/zabbix_api.c -o build/zabbix_api.o
gcc ... -c src/config.c -o build/config.o
g++ ... -c src/render.cpp -o build/render.o
gcc ... -c src/widget.c -o build/widget.o
gcc ... -c src/ui_login.c -o build/ui_login.o
gcc ... -c src/ui_select.c -o build/ui_select.o
gcc ... -c src/main.c -o build/main.o
g++ -o zabbix-desktop.exe build/*.o -lgdiplus -lwininet -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -lole32 -luuid -lcrypt32 -mwindows
```

### 使用方法

1. 运行 `zabbix-desktop.exe`
2. 首次运行会弹出登录对话框，填入 Zabbix API URL、用户名、密码
   - URL 格式：`http://your-zabbix-server/zabbix` 或 `https://...`
   - 程序会自动拼接 `/api_jsonrpc.php`
3. 登录成功后，右键系统托盘图标 → **"Add Widget..."**
4. 在弹出的选择窗口中：
   - 左侧选择或搜索主机
   - 右侧选择或搜索监控项
   - 选择 Widget 类型（Gauge / Card / Trend）
   - 点击 **"Add Widget"**
5. Widget 出现在桌面上，自动开始定时刷新数据
6. 右键 Widget 可：
   - **Always on Top**：切换置顶
   - **Configure...**：配置刷新间隔、透明度、颜色、阈值等
   - **Refresh Now**：立即刷新
   - **Remove Widget**：删除该 widget

### 项目结构

```
zabbix-desktop-info/
├── src/
│   ├── json.h / json.c          # 轻量级 JSON 解析器
│   ├── zabbix_api.h / .c        # Zabbix JSON-RPC API 客户端 (WinINet)
│   ├── config.h / .c            # 配置文件读写 + DPAPI 密码加密
│   ├── render.h / .cpp          # GDI+ 渲染：仪表盘/卡片/趋势图
│   ├── widget.h / .c           # Widget 透明窗口管理 (UpdateLayeredWindow)
│   ├── ui_login.h / .c          # 登录对话框
│   ├── ui_select.h / .c         # 主机/监控项选择对话框
│   └── main.c                   # 入口：系统托盘 + 消息循环
├── CMakeLists.txt
├── build.bat
├── LICENSE                      # Apache License 2.0
└── README.md
```

### Zabbix API 调用

| API 方法 | 用途 |
|----------|------|
| `user.login` | 登录获取 auth token（兼容 5.4+ 的 username 字段） |
| `host.get` | 获取主机列表 |
| `item.get` | 获取主机监控项（含最新值） |
| `history.get` | 获取历史趋势数据 |

### 配置文件

路径：`%APPDATA%\ZabbixDesktopInfo\config.json`

```json
{
  "zabbix_url": "http://192.168.1.100/zabbix",
  "zabbix_user": "Admin",
  "zabbix_pass": "enc:01000000D08C9DDF...",
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

### 密码安全

配置文件中的密码使用 **Windows DPAPI**（Data Protection API）加密存储：

- `CryptProtectData` 使用当前 Windows 用户的登录凭据加密密码
- 配置文件中存储 `enc:` 前缀 + hex 编码的密文，不含明文
- 加密后的密文**只能由同一 Windows 用户在同一台机器**解密
- 将配置文件拷贝到其他机器或其他用户下**无法解密**
- 旧的明文配置文件在下次保存时自动迁移为加密格式

> **注意**：Zabbix 的 `user.login` API 要求传入明文密码，因此无法使用真正的单向哈希（如 SHA-256）。DPAPI 是 Windows 平台保护密钥的标准方式（浏览器、邮件客户端等均使用此方式），在配置文件层面实现了等价的保护效果。

---

<a id="english"></a>

## English

### Overview

A Zabbix desktop real-time monitoring dashboard built with C + Win32 + GDI+. Log in to Zabbix, select host monitoring items, and display live data as transparent desktop widgets.

### Features

- **Three Widget Types**
  - **Gauge**: Circular pointer gauge with configurable warning/critical threshold colors (green/yellow/red)
  - **Card**: Rounded rectangle showing title and value
  - **Trend**: Line chart displaying historical trend data

- **Transparent Desktop Window**: Uses `UpdateLayeredWindow` + 32-bit ARGB DIB for per-pixel transparency
- **Always on Top**: Configurable always-on-top, floating above all windows
- **Win+D Anti-Minimize**: Widgets stay visible when Win+D is pressed, preventing minimization
- **Drag to Move**: Left-click and drag to move widget position, auto-saved
- **Click-Through**: Transparent areas pass clicks through to the desktop
- **System Tray**: Right-click tray icon to add widgets, change login settings, refresh all, exit
- **Persistent Config**: All settings saved to `%APPDATA%\ZabbixDesktopInfo\config.json`
- **Encrypted Password Storage**: Password encrypted with Windows DPAPI, no plaintext in config file
- **GUI Item Selection**: Host list (with search) → Item list (with search) → Select widget type
- **HTTPS Support**: Supports both HTTP and HTTPS, compatible with self-signed certificates

### Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C (C99) + C++ (GDI+ rendering layer) |
| Window | Win32 API (CreateWindowEx, UpdateLayeredWindow) |
| Graphics | GDI+ (Anti-aliased rendering) |
| HTTP | WinINet (HTTP/HTTPS support) |
| JSON | Custom lightweight JSON parser |
| Password Protection | Windows DPAPI (CryptProtectData / CryptUnprotectData) |
| Transparent Window | WS_EX_LAYERED + UpdateLayeredWindow + premultiplied ARGB |

### Build

#### Prerequisites
- MinGW GCC 6.3+ (or MSVC / Clang)
- Windows SDK (GDI+, WinINet, Common Controls, Crypt32)

#### Using build.bat (MinGW)
```bat
cd D:\Code\zabbix-desktop-info
build.bat
```

#### Using CMake
```bat
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

#### Manual Compilation
```bash
gcc -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600 -Isrc -O2 -Wall -c src/json.c -o build/json.o
gcc ... -c src/zabbix_api.c -o build/zabbix_api.o
gcc ... -c src/config.c -o build/config.o
g++ ... -c src/render.cpp -o build/render.o
gcc ... -c src/widget.c -o build/widget.o
gcc ... -c src/ui_login.c -o build/ui_login.o
gcc ... -c src/ui_select.c -o build/ui_select.o
gcc ... -c src/main.c -o build/main.o
g++ -o zabbix-desktop.exe build/*.o -lgdiplus -lwininet -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -lole32 -luuid -lcrypt32 -mwindows
```

### Usage

1. Run `zabbix-desktop.exe`
2. On first launch, a login dialog appears. Enter Zabbix API URL, username, and password
   - URL format: `http://your-zabbix-server/zabbix` or `https://...`
   - The program automatically appends `/api_jsonrpc.php`
3. After login, right-click the system tray icon → **"Add Widget..."**
4. In the selection dialog:
   - Select or search for a host on the left
   - Select or search for a monitoring item on the right
   - Choose widget type (Gauge / Card / Trend)
   - Click **"Add Widget"**
5. The widget appears on the desktop and starts auto-refreshing data
6. Right-click a widget to:
   - **Always on Top**: Toggle always-on-top
   - **Configure...**: Configure refresh interval, opacity, colors, thresholds, etc.
   - **Refresh Now**: Refresh immediately
   - **Remove Widget**: Remove the widget

### Project Structure

```
zabbix-desktop-info/
├── src/
│   ├── json.h / json.c          # Lightweight JSON parser
│   ├── zabbix_api.h / .c        # Zabbix JSON-RPC API client (WinINet)
│   ├── config.h / .c            # Config file I/O + DPAPI password encryption
│   ├── render.h / .cpp          # GDI+ rendering: gauge/card/trend
│   ├── widget.h / .c           # Widget transparent window (UpdateLayeredWindow)
│   ├── ui_login.h / .c          # Login dialog
│   ├── ui_select.h / .c         # Host/item selection dialog
│   └── main.c                   # Entry: system tray + message loop
├── CMakeLists.txt
├── build.bat
├── LICENSE                      # Apache License 2.0
└── README.md
```

### Zabbix API Calls

| API Method | Purpose |
|------------|---------|
| `user.login` | Login to get auth token (compatible with 5.4+ username field) |
| `host.get` | Get host list |
| `item.get` | Get host monitoring items (including last value) |
| `history.get` | Get historical trend data |

### Configuration File

Path: `%APPDATA%\ZabbixDesktopInfo\config.json`

```json
{
  "zabbix_url": "http://192.168.1.100/zabbix",
  "zabbix_user": "Admin",
  "zabbix_pass": "enc:01000000D08C9DDF...",
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

### Password Security

The password in the configuration file is encrypted using **Windows DPAPI** (Data Protection API):

- `CryptProtectData` encrypts the password using the current Windows user's login credentials
- The config file stores an `enc:` prefix + hex-encoded ciphertext, no plaintext
- The encrypted data can **only be decrypted by the same Windows user on the same machine**
- Copying the config file to another machine or another user **cannot be decrypted**
- Old plaintext config files are automatically migrated to encrypted format on next save

> **Note**: Zabbix's `user.login` API requires the plaintext password, so true one-way hashing (e.g., SHA-256) is not feasible. DPAPI is the standard Windows platform approach for protecting secrets (used by browsers, email clients, etc.), achieving equivalent protection at the configuration file level.

---

## License / 许可证

This project is licensed under the **Apache License, Version 2.0**.

See the [LICENSE](LICENSE) file for the full license text.

本项目基于 **Apache License 2.0** 协议开源。完整的许可证文本请参见 [LICENSE](LICENSE) 文件。

---

## AI Generation Disclosure / AI 生成声明

### 中文

**本项目代码由 AI 辅助生成。**

- 本项目的源代码主要由 AI 助手（WorkBuddy / CodeBuddy）辅助编写和调试生成
- AI 参与了代码编写、Bug 修复、功能实现等开发工作
- 项目的架构设计、技术选型和需求分析由人类开发者完成
- 人类开发者对所有提交的代码进行了审阅和测试

使用者应注意：
1. **AI 生成代码可能存在缺陷**：尽管经过测试和审阅，AI 生成的代码仍可能包含未发现的 Bug、逻辑错误或安全隐患
2. **使用者责任**：使用者在部署和运行本程序前，应自行审查代码、进行充分测试，并评估是否适合其使用场景
3. **不承担保证责任**：本程序按"原样"提供，不提供任何明示或暗示的保证
4. **安全风险**：本程序涉及网络通信和凭据存储，使用者应评估其安全风险并采取适当的安全措施

### English

**This project's code was generated with AI assistance.**

- The source code of this project was primarily written and debugged with the assistance of an AI assistant (WorkBuddy / CodeBuddy)
- AI participated in code writing, bug fixing, and feature implementation
- The project's architecture design, technology selection, and requirements analysis were done by the human developer
- The human developer reviewed and tested all committed code

Users should note:
1. **AI-generated code may contain defects**: Despite testing and review, AI-generated code may still contain undiscovered bugs, logic errors, or security vulnerabilities
2. **User responsibility**: Before deploying and running this program, users should review the code, conduct thorough testing, and evaluate its suitability for their use case
3. **No warranty**: This program is provided "as is", without any express or implied warranties
4. **Security risks**: This program involves network communication and credential storage. Users should assess security risks and take appropriate security measures

---

## Risk Disclaimer / 风险免责声明

### 中文

1. **网络安全风险**：本程序通过网络与 Zabbix 服务器通信，可能面临网络攻击、中间人攻击、数据泄露等风险。使用者应确保在安全的网络环境中使用，并采取适当的网络安全措施（如使用 HTTPS、VPN 等）。

2. **凭据存储风险**：尽管密码使用 Windows DPAPI 加密存储，但无法保证绝对安全。DPAPI 的安全性依赖于 Windows 用户账户的安全性。如果 Windows 账户被入侵，加密的密码可能被解密。建议使用者定期更换密码，并遵循最小权限原则配置 Zabbix 账户。

3. **Zabbix API 权限风险**：本程序使用 Zabbix API 进行操作，需要相应的 API 权限。使用者应确保使用的 Zabbix 账户仅具有必要的最小权限，避免使用超级管理员账户。

4. **自签名证书风险**：本程序默认忽略 HTTPS 自签名证书错误以便于使用，但这会降低安全性。在生产环境中，建议使用受信任的 CA 签发的证书，并考虑禁用证书忽略逻辑。

5. **系统稳定性风险**：AI 生成的代码可能存在内存泄漏、线程安全问题或其他未发现的缺陷，可能导致程序崩溃或系统不稳定。使用者应定期监控程序运行状态。

6. **适用性限制**：本程序在 Windows Vista 及以上版本（`_WIN32_WINNT=0x0600`）上开发和测试，不保证在其他 Windows 版本上的兼容性。

7. **免责声明**：在任何情况下，本项目的贡献者或 AI 工具提供商均不对因使用本程序而产生的任何直接、间接、附带、特殊或后果性损害承担责任。

### English

1. **Network Security Risk**: This program communicates with Zabbix servers over the network and may face risks such as network attacks, man-in-the-middle attacks, and data breaches. Users should ensure use in a secure network environment and take appropriate network security measures (e.g., HTTPS, VPN).

2. **Credential Storage Risk**: Although passwords are encrypted using Windows DPAPI, absolute security cannot be guaranteed. DPAPI's security relies on the security of the Windows user account. If the Windows account is compromised, encrypted passwords may be decrypted. Users are advised to change passwords regularly and configure Zabbix accounts following the principle of least privilege.

3. **Zabbix API Permission Risk**: This program uses the Zabbix API for operations and requires corresponding API permissions. Users should ensure that the Zabbix account used has only the minimum necessary permissions, avoiding the use of super administrator accounts.

4. **Self-Signed Certificate Risk**: This program ignores HTTPS self-signed certificate errors by default for ease of use, which reduces security. In production environments, it is recommended to use certificates issued by a trusted CA and consider disabling the certificate bypass logic.

5. **System Stability Risk**: AI-generated code may contain memory leaks, thread safety issues, or other undiscovered defects that may cause program crashes or system instability. Users should regularly monitor the program's running status.

6. **Compatibility Limitation**: This program was developed and tested on Windows Vista and later (`_WIN32_WINNT=0x0600`). Compatibility with other Windows versions is not guaranteed.

7. **Disclaimer**: In no event shall the contributors of this project or the AI tool provider be liable for any direct, indirect, incidental, special, or consequential damages arising from the use of this program.
