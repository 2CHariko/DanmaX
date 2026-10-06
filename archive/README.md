# 历史版本归档

- `python-qt/`：原 Python/PyQt6 实现、README、配置、样本、截图和手工测试。
- `plans/`：此前 Flutter 迁移方案，仅供历史参考。
- `python-qt/archive-manifest.json`：归档时源文件与资源的 SHA-256 校验清单。

新项目不依赖这些文件，不把归档加入 CMake 或测试发现路径。Python 虚拟环境、日志和缓存也已移入归档，但继续被 Git 忽略。

Python 虚拟环境含旧路径，移动后不保证脚本入口可运行。如需运行历史版本，应在该目录根据 `requirements.txt` 重建环境，并从该目录启动 `main.py`。本次未修改旧源码和配置来适配新路径。
