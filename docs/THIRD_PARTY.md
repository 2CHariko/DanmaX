# 第三方组件与分发状态

当前工具版本和下载校验见 `toolchain/dependencies.lock.json`。

- Qt 6.11.0：按所用模块适用的 Qt 许可证使用。开发 SDK 来自 Qt 官方预编译包，动态链接。SDK 内 `LICENSES`、模块元数据和官方源码分发为许可证核对依据。
- CMake 3.31.6：BSD-3-Clause，仅构建使用。
- Ninja 1.12.1：Apache-2.0，仅构建使用。
- Microsoft MSVC / Windows SDK：系统开发工具，不包含在项目源码或工具下载包内。
- Windows 自带 tar 和 PowerShell 7 为脚本执行前提，不额外安装解压库。

当前 package 脚本生成开发验收目录，不自动声称已具备公开发布的全部许可材料。公开分发前，核对实际部署的 Qt 模块和第三方组件，随包提供所需通知、许可证、对应源码获取方式及适用的其他材料。不得从开发机复制任意 MSVC DLL 代替官方运行库分发。

下载来源：Qt 官方 SDK 仓库、Kitware/CMake 官方 GitHub Release、ninja-build/ninja 官方 GitHub Release。锁定清单的 SHA-256 用于后续下载校验；Qt 包另外与官方 SHA-1 对照，CMake 包与官方 SHA-256 清单对照，Ninja 首次下载通过官方 HTTPS 来源获取并固定 SHA-256。
