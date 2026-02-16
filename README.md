# Super Download

一个类似 IDM 的多线程下载管理器，使用 C++17 + Qt6 + libcurl 构建。

## 功能特性

- 多线程分块下载，充分利用带宽
- 断点续传，支持 ETag/Last-Modified 校验
- 浏览器扩展集成（Chrome），自动拦截下载
- 自定义协议 `superdownload://`，程序未运行时自动启动
- 文件分类管理（视频、音乐、文档、压缩包、程序等）
- 全局限速、队列管理、计划下载
- 系统托盘、开机自启、下载完成通知
- 批量下载、导入/导出任务列表
- 现代化白色主题 UI

## 构建要求

- C++17 编译器（MinGW 13+ 或 MSVC 2019+）
- CMake 3.20+
- Qt 6.x（Widgets, Network 模块）
- Ninja（推荐）

libcurl 通过 CMake FetchContent 自动下载编译，无需手动安装。

## 构建命令

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/mingw_64"
cmake --build build --parallel
```

## 安装包

使用 Inno Setup 打包：

```bash
iscc installer.iss
```

输出：`dist/SuperDownload_Setup_1.0.0.exe`

## 浏览器扩展

`browser_extension/` 目录包含 Chrome 扩展，在 `chrome://extensions` 中以开发者模式加载即可。

## 许可证

MIT License
