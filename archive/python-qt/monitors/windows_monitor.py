# monitors/windows_monitor.py
import asyncio
from datetime import timedelta
import logging

# 导入抽象基类
from .base_monitor import BaseMediaMonitor

# 尝试导入Windows平台特定的库
try:
    import win32gui
    import win32process
    from psutil import Process
    from winsdk.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as MediaManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus
    )
    WINSDK_AVAILABLE = True
except ImportError:
    WINSDK_AVAILABLE = False
    # 如果缺少库，在代码加载时就给出提示
    logging.warning("缺少 Windows 平台所需的库 (winsdk, pywin32, psutil)。媒体监控功能将不可用。")


class MediaMonitorError(Exception):
    """模块自定义的异常基类。"""
    pass

class SessionInfo:
    """一个简单的数据类，用于存储媒体会话的详细信息。"""
    def __init__(self, title, artist, status, position, duration, aumid):
        self.title = title
        self.artist = artist
        self.status = status.name # PlaybackStatus 枚举成员的名称, e.g., "PLAYING"
        self.position = position # timedelta 对象
        self.duration = duration # timedelta 对象
        self.source_aumid = aumid # 来源应用的AUMID

    def __repr__(self):
        pos_str = self.format_time(self.position)
        dur_str = self.format_time(self.duration)
        return (f"<SessionInfo(aumid='{self.source_aumid}', status='{self.status}', "
                f"title='{self.title}', progress='{pos_str}/{dur_str}')>")

    @staticmethod
    def format_time(duration: timedelta) -> str:
        """将 timedelta 对象格式化为 HH:MM:SS"""
        ts = int(duration.total_seconds())
        return f"{ts // 3600:02d}:{(ts % 3600) // 60:02d}:{ts % 60:02d}"

class WindowsMediaMonitor(BaseMediaMonitor):
    """
    Windows平台的媒体监控器实现。
    使用Windows SMTC API来获取和筛选媒体会话信息。
    """
    def __init__(self):
        """
        初始化媒体监视器。
        如果 winsdk 不可用，将引发异常。
        """
        if not WINSDK_AVAILABLE:
            raise MediaMonitorError("winsdk 库未安装或不完整。请运行 'pip install winsdk'。")

    async def list_sessions(self) -> list[dict]:
        """
        异步列出所有当前活动的媒体会话的基本信息。
        """
        manager = await MediaManager.request_async()
        sessions = manager.get_sessions()
        session_list = []
        for session in sessions:
            try:
                info = await session.try_get_media_properties_async()
                # 只有包含标题的会话才是有意义的媒体会话
                if info and info.title:
                    session_list.append({
                        "aumid": session.source_app_user_model_id,
                        "title": info.title
                    })
            except Exception:
                # 某些会话可能在查询时失效，直接跳过
                continue
        return session_list

    async def get_current_session_info(self) -> SessionInfo | None:
        """
        异步获取当前SMTC托管的媒体会话的详细信息。
        """
        try:
            manager = await MediaManager.request_async()
            session = manager.get_current_session()
            if session:
                return await self._get_session_details(session)
        except Exception:
            return None
        return None

    async def _get_session_details(self, session) -> SessionInfo:
        """从一个会话对象中异步提取完整的媒体信息。"""
        info = await session.try_get_media_properties_async()
        timeline = session.get_timeline_properties()
        playback_info = session.get_playback_info()
        return SessionInfo(
            title=info.title, artist=info.artist,
            status=PlaybackStatus(playback_info.playback_status),
            position=timeline.position, duration=timeline.end_time,
            aumid=session.source_app_user_model_id
        )

    def get_foreground_window_aumid(self) -> str | None:
        """
        获取当前前台窗口的进程名，作为AUMID的代理。
        注意：这只是一个近似方法，并非所有进程名都等于其AUMID，但对于PotPlayer等应用有效。
        """
        if not WINSDK_AVAILABLE:
            return None
        try:
            hwnd = win32gui.GetForegroundWindow()
            if hwnd:
                _, pid = win32process.GetWindowThreadProcessId(hwnd)
                return Process(pid).name()
        except Exception:
            return None
        return None