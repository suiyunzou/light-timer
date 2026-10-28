# 开发说明

普通用户可直接下载 [Windows 便携版](https://github.com/suiyunzou/light-timer/releases/latest)。以下内容面向构建、测试和维护人员。

## 构建

需要 Windows、PowerShell 和 x64 MinGW-w64 C++17 工具链（含 `g++.exe` 与 `windres.exe`）。当前发布使用 MinGW GCC 8.1.0。

```powershell
# 已将 MinGW-w64 的 bin 目录加入 PATH
.\build.ps1

# 或显式指定编译器
.\build.ps1 -Compiler 'C:\Toolchains\mingw64\bin\g++.exe'

# 将验证产物写入独立目录
.\build.ps1 -Compiler 'C:\Toolchains\mingw64\bin\g++.exe' -OutputDirectory "$PWD\scratch\build-check"
```

默认产物为 `dist/LightTimer.exe`。脚本编译资源、静态链接编译器运行库，并自动运行 `--self-test` 计时引擎检查。发布 EXE 不依赖 Python 或外部图标文件。

链接参数中，`-luuid` 必须位于 `-loleacc` 前，避免当前 MinGW 导入库将接口 GUID 解析为导入跳板。

## 按改动选择测试

Python 仅用于测试。依赖见 `tests/requirements.txt`；涉及 MP3 的歌曲测试还需要 PATH 中的 ffmpeg。

```powershell
python -m pip install -r tests/requirements.txt
# 示例：修改时间视图后，测试实际构建的程序
python .\tests\time_views.py .\dist\LightTimer.exe
```

| 改动范围 | 相关检查 |
| --- | --- |
| 计时引擎 | 构建时自动执行的 `--self-test` |
| 倒计时、秒表、时钟切换 | `tests/time_views.py` |
| 悬停、点击与拖动 | `tests/hover_guard.py` |
| 到时与重新开始 | `tests/zero_finish.py` |
| 输入区域与托盘设置 | `tests/merged_controls.py`、`tests/tray_preferences.py` |
| 工作／个人模式与歌曲 | `tests/scene_modes.py` |
| 键盘、辅助功能、持久化、资源采样 | `tests/keyboard.py`、`tests/accessibility.py`、`tests/persistence.py`、`tests/profile.py` |

运行前阅读对应脚本的启动要求：部分脚本自行启动隔离实例，部分要求先启动 `LightTimer.exe --test`。持久化测试要求正式实例退出。不要同时运行多个桌面交互测试。

测试可能移动鼠标、打开菜单或显示全屏提醒，请在无需操作桌面时执行。结果写入 `scratch/`，不随源码发布。截图、日志和生成的配置可能包含本机路径，上传前必须检查。

## 实现与开销

- 使用系统单调时钟计算截止点和已用时间，不逐秒累加误差；睡眠时间计入计时。
- 计时数字约每秒更新，时钟按分钟更新。暂停、隐藏或按钮遮住数字时，减少不必要的重绘。
- 最后 5 秒的呼吸动画使用 20 fps；关闭系统动画时减少动效。
- 本地歌曲由 Windows 媒体驱动读取，结束或停止后关闭设备。
- 不用桌面截图模拟模糊，不调用 `timeBeginPeriod`，不主动压缩工作集。

以上是实现方式，不代表功耗或内存测量结论。进程 CPU 时间不等于实际功率，也不包括单独归因的桌面合成器 GPU 开销。

## 目录

| 路径 | 内容 |
| --- | --- |
| `src/main.cpp` | 窗口、托盘、设置、提醒及辅助功能 |
| `src/engine.hpp` | 倒计时、正向计时与刷新调度 |
| `src/paint.hpp` | 位图和矢量绘制 |
| `src/file_audio.hpp` | 本地歌曲播放与结束通知 |
| `src/app.rc`、`src/app.manifest` | 图标、版本与 Windows 清单 |
| `assets/` | 应用图标及生成脚本 |
| `tests/` | 原生交互与运行检查 |
| `docs/images/` | 经隐私检查的 README 截图及 GIF |
| `dist/`、`outputs/` | 本机构建和发布产物，不纳入 Git |
| `scratch/` | 临时构建、配置及原始测试记录，不纳入 Git |

图标由本项目绘制，字体使用系统字体。没有导入其他计时应用的源码或品牌资源。
