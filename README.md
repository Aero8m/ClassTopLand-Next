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
    <img src="https://img.shields.io/badge/Qt-6.9%2B-41CD52" alt="Qt 6.9+">
  </a>
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-GPLv2-4ec820" alt="GPLv2">
  </a>
  <img src="https://img.shields.io/badge/Build-Windows-0078D4" alt="Windows 构建">
</p>

<p align="center">
  <a href="#功能特性">功能特性</a> ·
  <a href="#开始使用">开始使用</a> ·
  <a href="#从源码构建">从源码构建</a> ·
  <a href="#数据与备份">数据与备份</a> ·
  <a href="https://github.com/Aero8m/ClassTopLand-Next/issues">反馈问题</a>
</p>

## 项目介绍

ClassTopLand Next 将当天课程、上下课倒计时和日期显示在桌面课程条上，适合在教室电脑上查看课程安排。通过系统托盘快捷面板，可以打开设置、编辑档案和切换课表。

项目使用 C++17、Qt Widgets 和 ElaWidgetTools-Plus 构建界面，使用 JSON 保存配置与档案，并支持 ClassTopLand tables.json 和 CSES 格式课表交换。下文描述的是当前源码实现；构建步骤以 Windows x64 为例。源码包含 Linux / macOS 的部分平台处理，但当前构建配置仍需适配，本文不承诺这些平台已完成构建与运行验证。

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 桌面课程条 | 置顶显示，支持展开与收起，收起后以紧凑形式显示课程状态 |
| UIAccess 增强置顶 | Windows 下可在课程条设置中开关，切换后自动重启，开启时请求管理员授权 |
| 课程状态与倒计时 | 显示上课前、上课中、课间、今日课程结束及无课程等状态 |
| 上下课提醒 | 在上课前 2 分钟、上课和下课时显示课程条内提醒 |
| 组件布局 | 添加、移除、启用或停用日期、课程查看和文本提示组件；调整顺序、课程条高度并预览布局 |
| 科目管理 | 编辑科目名称、简称与教师信息 |
| 时间线编辑 | 管理上课时段，支持画布调整、缩放、撤销与重做，并将时间线应用到当天课表 |
| 课表管理 | 管理多张周课表、适用星期和课程时间，支持全部周、单周和双周模式 |
| 调休 | 从托盘面板打开独立窗口，将某星期课表应用到指定日期，支持空课表与恢复 |
| 换课 | 在独立窗口中交换指定日期的两节课科目，保留原时段，不改变常规周课表 |
| 档案管理 | 创建空白档案、复制已保存档案、删除档案，以及切换档案并重启 |
| 导入与导出 | 支持原生 JSON 完整档案、ClassTopLand tables.json 常规课表及 CSES v1 YAML 课表 |
| 外观设置 | 自定义主题色、取用当前系统强调色，选择跟随系统、浅色或深色模式 |

### 课表匹配说明

- **自动匹配**：按当天星期与周模式选择课表；多张课表同时匹配时，需要在托盘快捷面板中手动选择。
- **手动选择**：在快捷面板的「切换课程表」中指定周课表，选择会保存到当前档案。恢复「自动匹配」后重新按日期匹配。
- **单双周限制**：当前界面尚未提供第一周日期配置，因此单周 / 双周课表暂时需要手动选择，不能仅依靠自动匹配运行。
- **日期安排**：调休与换课保存在当前档案中，仅对指定日期生效，重启后保留。来源日的科目、时间或课程数量变化，以及来源被删除时，关联安排会自动取消并提示；仅改名称或课程列表顺序不取消。可在调休窗口或换课窗口恢复该日期全部特殊安排。
- **自动清理**：启动时、每分钟及档案提交时清理过去日期记录，保留今天与未来安排；课程条关闭时也会清理。非当前档案在加载时清理。

## 开始使用

可在 [Releases](https://github.com/Aero8m/ClassTopLand-Next/releases) 页面查看发布包；使用源码时，请参照下方构建步骤。

1. 启动程序。首次运行会在用户主目录下创建默认配置和 `Default` 档案。
2. 单击或右击系统托盘中的程序图标，打开快捷面板，进入「档案编辑」。
3. 在「基本信息」中填写档案名称，在「科目管理」中添加科目，再到「课表管理」创建周课表、设置适用星期并添加课程。也可以先在「时间线管理」中整理时段，再应用到当天课表。
4. 点击「保存」或按 `Ctrl+S` 保存档案，课程条随即刷新。编辑窗口中的未保存更改需要保存后才会生效。
5. 在「设置 → 课程条」中调整组件和高度，在「设置 → 外观」中调整主题。课程条左侧按钮用于展开与收起。

也可以在「设置 → 档案管理 → 导入档案」中导入 [示例档案](./examples/profiles/Test.json)，再选中它并点击「切换并重启」。示例包含多种科目与课程时段，可用于体验课程显示。

关闭设置或编辑窗口后，程序仍在托盘中运行。需要结束程序时，请使用快捷面板底部的退出按钮；切换档案会重启程序，并先处理编辑窗口中的未保存更改。

### UIAccess 增强置顶（Windows）

在「设置 → 课程条 → 基本设置」中开启「UIAccess 增强置顶」，程序会请求当前用户的管理员授权，并在新实例初始化成功后自动重启。关闭开关也会自动重启，撤销 UIAccess 并恢复普通置顶。未保存的档案编辑会先提示处理；取消退出、取消授权或权限获取失败时保留原实例和原设置。

开关默认关闭，并保存在 `MainConfig.json` 的 `courseBarConfig.uiAccessEnabled` 中。开启后，从桌面快捷方式重新启动通常仍需 UAC 授权；已取得 UIAccess 时，托盘重启和切换档案会复用权限。冷启动获取权限失败时回退为普通置顶并保存关闭状态；如果配置只读，则仅本次运行采用回退状态，并说明磁盘配置未改变。

实现参考 [killtimer0/uiaccess](https://github.com/killtimer0/uiaccess) 的令牌获取思路，无需代码签名或固定安装目录。该模式的最终实例保留当前用户的管理员权限，要求同一用户授权，不支持改用另一个管理员账户。关闭时通过 Windows 桌面 Shell 启动普通实例；系统策略或桌面 Shell 不可用时会提示切换失败。此功能不保证覆盖独占全屏程序或 UAC 安全桌面。非 Windows 平台禁用该开关。

## 从源码构建

### 环境要求

| 依赖 | 要求 / 用途 |
| --- | --- |
| C++ 编译器 | 支持 C++17；以下 Windows 示例使用 MSVC 2022 x64 工具链 |
| Qt | 6.9 或更新版本，包含 Core、Gui、Widgets 及匹配版本的 Widgets 私有开发文件；当前工程路径使用 Qt 6.9.3 `msvc2022_64` |
| CMake | 4.0 或更新版本，与根目录的最低版本声明一致 |
| Ninja | 用于下方的单配置构建命令 |
| Git | 克隆仓库及初始化依赖子模块 |
| ElaWidgetTools-Plus | Git 子模块，随项目源码构建，默认生成动态库 |
| yaml-cpp | Git 子模块，随项目源码静态构建，版本由子模块提交固定 |

克隆仓库及初始化子模块时，需要能访问 GitHub 以获取依赖；子模块就绪后，CMake 配置无需下载 yaml-cpp。Qt 套件必须与编译器及目标架构匹配；使用 MSVC 套件时，还需安装对应的 Visual Studio C++ 构建工具和 Windows SDK。

### 1. 获取源码与子模块

```powershell
git clone --recurse-submodules https://github.com/Aero8m/ClassTopLand-Next.git
cd ClassTopLand-Next
```

已经克隆仓库时，在项目根目录补充初始化：

```powershell
git submodule update --init --recursive
```

### 2. 设置 Qt 路径

当前 [CMakeLists.txt](./CMakeLists.txt) 中直接设置了 Qt 路径：

```cmake
set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/msvc2022_64/lib/cmake")
```

如果本机安装位置不同，请先将这一行改为实际 Qt 套件的 `lib/cmake` 目录。当前写法会覆盖命令行传入的 `-DCMAKE_PREFIX_PATH=...`，因此仅传入该参数不能替代这一步。

### 3. 配置并编译

在 **Visual Studio 2022 Developer PowerShell** 中启用 x64 构建环境，确保 `cmake`、`ninja` 与 `cl` 可用，然后在项目根目录执行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

可执行文件位于 `build/ClassTopLand-Next.exe`。调试构建可以另用 `build-debug` 目录，并将 `CMAKE_BUILD_TYPE` 改为 `Debug`。

这里使用单配置的 Ninja 生成器，因为当前 Windows DLL 复制逻辑依据 `CMAKE_BUILD_TYPE` 判断 Debug / Release。更换编译器、生成器或 Qt 套件时，请使用新的构建目录。

Windows 构建应在正常桌面权限的 IDE 或开发者终端中进行。沙箱生成的构建目录可能继承 Low（低完整性）标签，使 EXE 从 CLion 或资源管理器启动后仍处于低权限，导致配置写入被拒绝、托盘异常和 UIAccess 交接失败。构建后会自动把本项目 EXE 的完整性标签设为 Medium，并保留其原有访问权限；无法完成时构建报错，避免把受限产物当作可运行版本。无需始终以管理员身份运行程序；只有启用增强置顶时才请求提权。

### 4. 部署与运行

构建脚本会复制 ElaWidgetTools 动态库及部分 Qt DLL / 平台插件。为补齐 Qt 运行依赖，可继续执行对应套件的 `windeployqt`（路径与上一步的 Qt 安装保持一致）：

```powershell
& "C:/Qt/6.9.3/msvc2022_64/bin/windeployqt.exe" --release .\build\ClassTopLand-Next.exe
.\build\ClassTopLand-Next.exe
```

分发时请保留部署后的 DLL 和插件目录，不要只复制可执行文件。Debug 构建请改用对应构建目录和 `windeployqt --debug`。

## 数据与备份

程序将数据保存在用户主目录下的 `ClassTopLand-Next_Data`，与可执行文件所在目录分开：

```text
ClassTopLand-Next_Data/
├── MainConfig.json       # 当前档案、课程条布局及外观设置
└── profiles/
    ├── Default.json      # 首次运行创建的默认档案
    └── <档案文件名>.json  # 科目、时间线、课表及手动选择的周课表
```

Windows 下通常为 `%USERPROFILE%\ClassTopLand-Next_Data`。备份整个目录可以保留设置与所有档案；手动修改或恢复文件前，请先退出程序，避免退出时保存的数据覆盖手动更改。

| 交换格式 | 适用场景 | 注意事项 |
| --- | --- | --- |
| 原生 JSON（`.json`） | 完整档案备份、档案迁移 | 保存档案名称、科目、时间线、周课表及当前周课表选择；应用外观与课程条配置另存于 `MainConfig.json` |
| ClassTopLand tables.json（`.json`） | 与旧版 ClassTopLand 交换常规周课表 | 导入自动识别；导出选择一个周课表，仅保存课程名称与分钟级时间，不保留单双周规则及档案元数据；暂不支持导入非空附加课表 |
| CSES v1（`.yaml` / `.yml`） | 与其他支持 CSES 的课表工具交换 | 交换科目与日课表，不保留档案名称、周课表分组名称和 ID、时间线及当前周课表选择；导入时忽略教室字段 |

导入会创建新档案，不会自动切换或覆盖已有档案。复制和导出使用档案的**已保存内容**，操作前请先保存编辑窗口中的更改。

### ClassTopLand tables.json

JSON 导入按内容区分原生档案与旧版课表，文件不必命名为 `tables.json`。导出时选择“ClassTopLand 课表”格式（与原生 JSON 使用相同扩展名），再选择周课表并确认转换说明。默认选中档案已保存的手动课表，否则选中第一个周课表。

```json
{
  "Mon": [{ "name": "语文", "start": "08:00", "end": "08:45" }],
  "Tue": [],
  "Wed": [],
  "Thu": [],
  "Fri": [],
  "Sat": [],
  "Sun": [],
  "appendixTables": {}
}
```

导入至少需要一个 `Mon`～`Sun` 星期键；缺失星期补为空课表，所有课程按开始时间排序。课程名称不能为空，时间必须为 `HH:mm`，开始早于结束，不能跨日或重叠。导入后建立“常规课表”（全部周次），按课程名称生成科目，简称默认取首字，教师为空，不生成时间线。`appendixTables` 可以缺省或为空对象；空附加课表会被忽略并提示，包含课程的附加课表会导致整个导入失败。

导出仅包含所选周课表，固定输出七个星期键和空的 `appendixTables`。不保留其他周课表、单双周规则、档案名称、周课表及日课表名称、ID、科目简称与教师、未使用科目、时间线或当前课表选择。非零秒会直接截去，确认窗口会列出受影响课程；截去秒后起止时间落在同一分钟的课程无法导出。需要完整备份时请使用原生 JSON。

## 常见问题

- **找不到 Qt / WidgetsPrivate**：确认 Qt 套件安装完整，私有开发文件与 Qt 版本一致，并检查 `CMakeLists.txt` 中的路径及编译器架构。
- **找不到 ElaWidgetTools 或 yaml-cpp 源码**：执行 `git submodule update --init --recursive`，确认子模块已完整下载。
- **子模块下载失败**：检查 GitHub 网络访问和 Git 是否可用，然后重新执行 `git submodule update --init --recursive`。
- **启动时提示缺少 DLL 或 Qt 平台插件**：使用同一 Qt 套件的 `windeployqt` 重新部署，并检查 ElaWidgetTools 动态库是否与程序一同保留。
- **显示「今天没有课程」或「课表不可用」**：检查当天是否有课程、科目和起止时间是否有效，以及是否存在重复匹配；单双周课表需先在快捷面板中手动选择。
- **托盘中找不到图标**：检查 Windows 隐藏图标区域。关闭课程条显示后，仍可从托盘打开设置并重新启用。

## 项目结构

```text
ClassTopLand-Next/
├── main.cpp                 # 应用入口与重启处理
├── CMakeLists.txt           # 构建配置与依赖管理
├── resourecs.qrc            # Qt 资源清单
├── res/                     # 图标、提醒图片与启动标识
├── examples/profiles/       # 示例档案
├── src/
│   ├── Core/                # 数据模型、配置、档案、课程刷新、主题与日志
│   ├── CourseBar/           # 课程条与内置组件
│   ├── ProfileEditWindow/   # 档案编辑、时间线画布与编辑会话
│   ├── SettingsWindow/      # 档案管理、外观、课程条设置与关于窗口
│   ├── TaskbarTrayMenu/     # 托盘快捷面板
│   └── Utils/               # 时间转换与平台背景效果辅助
└── third_party/
    ├── ElaWidgetTools-Plus/ # UI 组件库子模块
    └── yaml-cpp/           # YAML 解析库子模块
```

## 反馈与贡献

欢迎通过 [Issues](https://github.com/Aero8m/ClassTopLand-Next/issues) 报告问题或提出建议，通过 [Pull Requests](https://github.com/Aero8m/ClassTopLand-Next/pulls) 提交改进。

报告问题时，请说明系统版本、Qt / 编译器版本、使用的提交或发布版本、复现步骤和预期结果；界面问题可附截图，启动或构建问题可附控制台输出。提交代码前，请确认应用能构建并验证涉及的功能。

## 致谢

- [Qt](https://www.qt.io/)：应用框架与 Qt Widgets。
- [ElaWidgetTools-Plus](https://github.com/Aero8m/ElaWidgetTools-Plus)：项目使用的 UI 组件库，基于 [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) 扩展。
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)：CSES YAML 解析与生成。

## 许可证

本项目采用 [GNU General Public License v2](./LICENSE) 开源。复制、修改或分发本项目时，请遵守许可证中的相应条款。

第三方依赖遵循各自的许可证，详见相应项目及依赖目录中的许可证文件。
