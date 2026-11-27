<p align="center">
  <img width="128" alt="ClassTopLand Next 图标" src="./res/images/icon.png">
</p>

<h1 align="center">ClassTopLand Next</h1>

<p align="center">
  基于 Qt 6 和 ElaWidgetTools-Plus 的桌面课程显示组件
</p>

<p align="center">
  <a href="https://github.com/Aero8m/ClassTopLand-Next">
    <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17">
  </a>
  <a href="./CMakeLists.txt">
    <img src="https://img.shields.io/badge/Qt-6.9%2B-00599C" alt="Qt 6.9+">
  </a>
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-GPLv2-00599C" alt="GPLv2">
  </a>
  <img src="https://img.shields.io/badge/Build-Windows-00599C" alt="Windows 构建">
</p>


## 下载与安装

在这两处下载 Windows x64 安装包：[GitHub Releases][releases] · [Aero8m Project Autobuild][autobuild]

如果启动时提示缺少 Visual C++ 运行库，请安装 [Visual C++ 运行库][vc-runtime]

当软件的提交为4f8d33b以后的提交，可以在「设置 → 更新」检查更新，也可以开启自动更新。开启后，程序会在后台下载新版，并在通过托盘退出时安装。

## 设置第一张课表

1. 启动程序，点击 Windows 系统托盘里的图标，打开「档案编辑」。
2. 在「科目管理」添加科目，然后在「课表管理」新建周课表，选择适用的星期，填写课程和上课时间。
3. 点击「保存」或按 `Ctrl+S`，课程条会显示当前档案的课程安排。

已有课表时，可在「设置 → 档案管理 → 导入档案」导入原生 JSON、旧版 ClassTopLand 的 `tables.json` 或 CSES YAML。导入会创建新档案，选中后点击「切换并重启」即可使用。

关闭设置或档案编辑窗口后，程序仍在托盘中运行。要结束程序，请在托盘快捷面板中选择退出。

## 常用功能

- **单双周课表**　先在「档案编辑 → 基本信息 → 学期设置」填写学期第一天，再设置周课表适用的周次。托盘的「切换课程表」可选「自动匹配」或手动指定。
- **调休、换课与代课**　从托盘调整指定日期。可以套用其他星期的课表、跨天交换课程，或将某一节替换为其他科目。调整只对指定日期生效。
- **课程条与外观**　在「设置 → 课程条」调整显示内容、顺序和高度，也可开启需要管理员授权的增强置顶。在「设置 → 外观」切换配色。
- **提醒与桌面倒计时**　在「设置 → 提醒」配置上下课和课前提醒，也可按星期、科目或节次自定义。在「设置 → 桌面倒计时」为考试等活动设置一个独立的倒计时。
- **集控**　在「设置 → 集控」输入管理员提供的配对码，即可接收统一下发的课表和展示设置。配对后需重启一次，切换到云端档案。使用支持远程更新的 Autobuild 安装版时，管理员也可下发应用更新。
- **窗口拦截**　在「设置 → 窗口拦截」可临时隐藏窗口，或保存规则自动隐藏匹配的窗口。

## 插件与 AI Agent

通过托盘的「插件中心」管理 JavaScript 插件。安装前请确认插件来源和申请的权限。开发者可参考仓库里的示例和文档。[示例插件](./examples/plugins/) · [插件开发文档](./docs/plugins/README.md)

在「设置 → AI Agent → 模型供应商」配置模型和 API Key，再从托盘打开「AI Agent」。它可以对话、协助开发插件，并按会话权限读取文件、修改文件或执行命令。新会话默认使用「审批」模式，修改文件和执行命令前会请求确认。工具结果会发送给当前模型。

## 数据备份

在「设置 → 档案管理」将档案导出为原生 JSON，可备份科目、课表和提醒规则。若要一并备份应用设置和插件，请先退出程序，再复制用户主目录下的 `ClassTopLand-Next_Data`。使用过集控的设备应妥善保管整份备份，其中包含设备凭证。

## 从源码构建

Windows x64 构建使用 MSVC 2022、Qt 6.9.3（`msvc2022_64`）、CMake 4.0 或更新版本，以及 Ninja。请在启用了 x64 工具链的 Visual Studio 2022 Developer PowerShell 中执行。

```powershell
git clone --recurse-submodules https://github.com/Aero8m/ClassTopLand-Next.git
cd ClassTopLand-Next
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

依赖通过 Git 子模块随源码获取，包括 ElaWidgetTools-Plus、yaml-cpp 和 libcurl。如果已克隆仓库，可运行 `git submodule update --init --recursive` 补齐子模块。Qt 安装在其他位置时，在配置命令中用 `-DCMAKE_PREFIX_PATH` 指定 Qt 套件目录。编译完成后可运行 `build/ClassTopLand-Next.exe`。[Windows 自动构建流程](./.github/workflows/windows-autobuild.yml)

## 反馈与贡献

欢迎通过 [Issues](https://github.com/Aero8m/ClassTopLand-Next/issues) 报告问题或提出建议，通过 [Pull Requests](https://github.com/Aero8m/ClassTopLand-Next/pulls) 提交改进。

报告问题时，请说明系统版本、使用的提交或发布版本、复现步骤和预期结果；界面问题可附截图，启动或构建问题可附控制台输出。提交代码前，请确认应用能构建并验证涉及的功能。

## 致谢

- [Qt](https://www.qt.io/)：应用框架与 Qt Widgets。
- [ElaWidgetTools-Plus](https://github.com/Aero8m/ElaWidgetTools-Plus)：项目使用的 UI 组件库，基于 [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) 扩展。
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)：CSES YAML 解析与生成。
- [curl](https://github.com/curl/curl): 自动更新使用的下载库

## 许可证

本项目采用 [GNU General Public License v2](./LICENSE) 开源。复制、修改或分发本项目时，请遵守许可证中的相应条款。

[releases]: https://github.com/Aero8m/ClassTopLand-Next/releases
[autobuild]: https://autobuild.aero8m.cn/ClassTopLand-Next
[vc-runtime]: https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist?view=msvc-170
[issues]: https://github.com/Aero8m/ClassTopLand-Next/issues
[pulls]: https://github.com/Aero8m/ClassTopLand-Next/pulls
