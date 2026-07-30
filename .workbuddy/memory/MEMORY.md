# Zabbix Desktop Info - 项目长期记忆

## 项目概述
C + Win32 + GDI+ + UpdateLayeredWindow 实现的 Zabbix 桌面实时监控仪表盘程序

## 技术栈
- 语言: C (C99) + C++ (仅 GDI+ 渲染层 render.cpp)
- 编译器: MinGW GCC 6.3.0 (C:\MinGW\bin)
- 构建工具: build.bat 或 CMake
- 编译定义: -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600

## 架构
- json.c: 自实现 JSON 解析器
- zabbix_api.c: Zabbix JSON-RPC 客户端 (WinINet)
- config.c: JSON 配置文件 (%APPDATA%\ZabbixDesktopInfo\config.json)
- render.cpp: GDI+ 渲染 (仪表盘/卡片/趋势图)
- widget.c: Widget 透明窗口管理 (UpdateLayeredWindow)
- ui_login.c: 登录对话框
- ui_select.c: 监控项选择对话框
- main.c: 入口 + 系统托盘

## 关键技术决策
- GDI+ 是 C++ API，render.cpp 用 C++ 编译，通过 C 接口暴露
- JSON 内存管理: json_object_set/json_array_push 浅拷贝+free容器，调用后不要再 free 传入值
- UpdateLayeredWindow 需要 premultiplied alpha，GDI+ 绘制后手动 premultiply
- GDI+ FillEllipse/AddLine 有 REAL/INT 重载歧义，需显式 cast
- MinGW 无 winhttp.h，使用 wininet.h 替代
- 链接库: -lgdiplus -lwininet -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -lole32 -luuid -mwindows
