# 第三方组件与分发状态

工具版本和下载校验见 `toolchain/dependencies.lock.json`，静态 Qt 的匹配源码与特性见 `toolchain/qt-static.lock.json`。

- Qt 6.11.0：常规 Debug/Release 使用官方动态 SDK；用户授权的单 EXE 构建使用官方 qtbase、qtshadertools、qtdeclarative 源码，自行编译静态 Qt。按实际模块和第三方代码适用许可证核对，不能仅凭“Qt 开源”认定所有文件的许可相同。源码的 `LICENSES`、REUSE 元数据和各子目录 `qt_attribution.json` 为核对依据。
- Qt Shader Tools：构建 QML/Quick 所需；qsb 等工具不随应用分发。应用中实际链接的相关库/第三方代码仍纳入组件审查。
- 在线弹幕使用现有锁定 Qt 的 Network、JSON 和 Schannel TLS 后端；无新增下载依赖或 OpenSSL。静态源码和动态 SDK 清单已包含对应 qtbase。部署需保留 TLS 后端；回环测试证书由本项目生成，仅测试使用。
- CMake 3.31.6：BSD-3-Clause，仅构建使用。
- Ninja 1.12.1：Apache-2.0，仅构建使用。
- Microsoft MSVC / Windows SDK：明确声明的系统开发工具。静态应用使用 `/MT`；Windows 系统库和驱动不随包复制。动态包仍需官方 VC++ x64 运行库分发方案。
- Windows 自带 tar、PowerShell 7 为开发脚本前提，不是静态应用运行依赖。
- 保留的自解压格式使用 Windows 系统 makecab、Cabinet/FDI、BCrypt，未引入第三方压缩库。

`package.ps1` 默认生成静态单 EXE 开发验收产物；配套 `materials-static-<时间戳>/` 包含项目 LICENSE、说明、Qt 模块 LICENSES、通过 SHA-256 检查的匹配源码归档、应用本次源码快照、锁定清单和离线重新构建说明。应用源码快照覆盖当前未提交的源码变更，不包含 `.local/` 私人设置、历史归档或开发 SDK。源码/构建材料与 EXE 分开保存，不要求运行时解压。

公开发行前仍须核对实际静态链接组件和第三方代码，确认通知、许可证、对应源码、重新编译/链接及适用的其他义务。静态链接本身不能替代这些材料；也不直接推定必须改变本项目 MIT 许可。已有材料是审查输入，不宣称完整许可验收。

`-Format directory` 保留动态目录便于替换 Qt DLL；`-Format single-exe` 保留旧自解压方案，启动器校验并恢复嵌入的动态依赖，其修改/替换行为也需结合实际发行方式核对。旧官方二进制 SDK 未提供 LICENSES 目录，不能把条件复制视为材料齐备。结构和验证边界见 [便携打包](PACKAGING.md)。

来源：Qt 官方 SDK 仓库及源码分发、Kitware/CMake 官方 GitHub Release、ninja-build/ninja 官方 GitHub Release。静态源码 SHA-256 与官方源码校验清单核对；常规 Qt SDK 另与官方 SHA-1 对照，CMake 与官方 SHA-256 对照，Ninja 通过官方 HTTPS 获取后固定 SHA-256。所有准备脚本在解压前校验下载内容，构建/打包阶段不联网。
