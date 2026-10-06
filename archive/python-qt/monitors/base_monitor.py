# monitors/base_monitor.py
from abc import ABC, abstractmethod

class BaseMediaMonitor(ABC):
    """
    媒体监控器的抽象基类。
    定义了所有平台特定的监控器必须实现的通用接口。
    这使得控制器代码可以与具体的实现解耦，便于未来扩展到其他操作系统。
    """

    @abstractmethod
    async def list_sessions(self) -> list[dict]:
        """
        异步列出所有当前活动的媒体会话。
        主要用于UI，帮助用户发现并选择目标播放器。

        Returns:
            list[dict]: 一个包含字典的列表，每个字典应至少包含 'aumid' 和 'title' 键。
        """
        pass

    @abstractmethod
    async def get_current_session_info(self) -> object | None:
        """
        异步获取当前系统主要媒体会话的详细信息。
        返回的对象应包含播放状态、进度等信息。

        Returns:
            object | None: 一个包含会话信息的对象，或在没有活动会话时返回 None。
        """
        pass

    def get_foreground_window_aumid(self) -> str | None:
        """
        获取当前前台窗口的应用标识符（AUMID）。
        这是一个可选实现的方法，主要用于判断播放器窗口是否在前台。

        Returns:
            str | None: 应用标识符字符串，或在无法获取时返回 None。
        """
        return None