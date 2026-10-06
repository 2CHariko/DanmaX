# danmaku_controller.py
import asyncio
import bisect
import os
import psutil
import logging
import sys
import threading
from datetime import timedelta

from PyQt6.QtCore import QObject, QThread, pyqtSignal

# 从本地模块导入
from config_loader import get_config
from danmaku_parser import load_from_xml
from danmaku_renderer import DanmakuWindow
from danmaku_models import DanmakuData

from monitors.base_monitor import BaseMediaMonitor
IS_WINDOWS = sys.platform == 'win32'
if IS_WINDOWS:
    from monitors.windows_monitor import WindowsMediaMonitor, MediaMonitorError
else:
    logging.warning("非Windows平台，媒体同步功能将不可用。")
    class MediaMonitorError(Exception): pass
    class WindowsMediaMonitor(BaseMediaMonitor):
        def __init__(self): logging.info("使用空的媒体监控器。")
        async def list_sessions(self) -> list[dict]: return []
        async def get_current_session_info(self) -> object | None: return None

class MediaSyncWorker(QObject):
    """
    媒体同步工作者。在一个独立的QThread中运行，避免阻塞主GUI线程。
    它负责周期性地调用监控器的异步方法来获取媒体信息。
    """
    session_info_updated = pyqtSignal(object)

    def __init__(self, monitor: BaseMediaMonitor):
        super().__init__()
        self.monitor = monitor
        self._is_running = True
        self.loop = None
        self.main_task = None

    async def _loop_logic(self):
        """
        异步循环，持续获取媒体信息。
        【已修正】增加了对 CancelledError 的捕获。
        """
        logging.info("媒体同步工作线程循环已启动。")
        while self._is_running:
            try:
                # 等待媒体信息
                info = await self.monitor.get_current_session_info()
                
                # 在 await 之后再次检查标志，因为在等待期间可能已经被停止
                if not self._is_running:
                    break
                
                # 发送获取到的信息
                self.session_info_updated.emit(info)
                
                # 短暂休眠，避免CPU占用过高
                await asyncio.sleep(0.1)

            except asyncio.CancelledError:
                # 当任务被外部（如 hot_reload）取消时，会进入这里。
                # 这是预期的行为，记录日志并跳出循环，让线程干净地结束。
                logging.info("媒体同步任务被取消，正常关闭中...")
                break
            except Exception as e:
                # 捕获其他在获取媒体信息时可能发生的错误
                logging.error(f"在工作线程中获取媒体信息时出错: {e}")
                self.session_info_updated.emit(None)
                try:
                    # 如果发生错误，等待稍长一点时间再重试
                    await asyncio.sleep(1)
                except asyncio.CancelledError:
                    # 如果在等待期间也被取消，同样跳出循环
                    logging.info("媒体同步任务在错误后等待时被取消。")
                    break
                    
        logging.info("媒体同步工作线程循环已正常退出。")

    def run(self):
        """此方法在QThread启动后被调用。"""
        try:
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
            self.main_task = self.loop.create_task(self._loop_logic())
            self.loop.run_until_complete(self.main_task)
        except Exception as e:
            # 在这里捕获 _loop_logic 中未处理的意外错误
            logging.error(f"asyncio 事件循环中发生意外错误: {e}")

    def stop(self):
        """请求停止工作线程的循环。"""
        logging.info("正在请求停止媒体同步工作线程...")
        self._is_running = False
        if self.main_task and not self.main_task.done():
            self.main_task.cancel()


class DanmakuController(QObject):
    error_occurred = pyqtSignal(str)
    sessions_discovered = pyqtSignal(list)

    def __init__(self):
        super().__init__()
        self.config = get_config()
        
        try:
            self.monitor: BaseMediaMonitor = WindowsMediaMonitor()
        except MediaMonitorError as e:
            logging.error(f"媒体监控器初始化失败: {e}")
            self.error_occurred.emit(f"媒体监控器初始化失败:\n{e}\n同步功能将不可用。")
            self.monitor = None

        self._self_proc_name = psutil.Process(os.getpid()).name().lower()
        
        self.renderer: DanmakuWindow | None = None
        self.all_danmaku: list[DanmakuData] = []
        self.danmaku_start_times: list[float] = []

        self._danmaku_idx = 0
        self._last_known_position = -1.0
        self._is_running_flag = False
        
        self._worker_thread: QThread | None = None
        self._worker: MediaSyncWorker | None = None
        
    def _setup_worker(self):
        if not self.monitor: return
        logging.debug("正在设置新的工作线程和工作者对象...")
        self._worker_thread = QThread()
        self._worker = MediaSyncWorker(self.monitor)
        self._worker.moveToThread(self._worker_thread)
        self._worker_thread.started.connect(self._worker.run)
        self._worker.session_info_updated.connect(self._on_session_info_received)
        self._worker_thread.finished.connect(self._worker_thread.deleteLater)
        self._worker_thread.finished.connect(self._worker.deleteLater)

    def is_running(self) -> bool:
        return self._is_running_flag

    def start(self, danmaku_path: str):
        if self._is_running_flag:
            logging.warning("弹幕已经正在运行。")
            return
        logging.info("正在初始化弹幕...")
        self.all_danmaku = load_from_xml(danmaku_path)
        if not self.all_danmaku:
            msg = f"无法从 '{danmaku_path}' 加载或解析弹幕文件。"
            logging.error(msg)
            self.error_occurred.emit(msg + "\n请检查文件路径或文件格式是否正确。")
            return
        self.danmaku_start_times = [d.start_time for d in self.all_danmaku]
        self.renderer = DanmakuWindow(total_danmaku_count=len(self.all_danmaku))
        self.renderer.show()
        self._is_running_flag = True
        if self.monitor:
            self._setup_worker()
            self._worker_thread.start()
        logging.info("弹幕已启动。")

    def stop(self):
        if not self._is_running_flag: return
        if self._worker: self._worker.stop()
        if self._worker_thread and self._worker_thread.isRunning():
            self._worker_thread.quit()
            if not self._worker_thread.wait(500):
                logging.warning("工作线程未在500毫秒内正常停止，正在强制终止。")
                self._worker_thread.terminate()
                self._worker_thread.wait()
        self._worker_thread = None
        self._worker = None
        if self.renderer:
            self.renderer.close()
            self.renderer = None
        self.all_danmaku.clear()
        self.danmaku_start_times.clear()
        self._danmaku_idx = 0
        self._last_known_position = -1.0
        self._is_running_flag = False
        logging.info("弹幕已停止并清理资源。")

    def _format_time_for_debug(self, duration: timedelta) -> str:
        """辅助方法，用于格式化timedelta为时间字符串。"""
        if not isinstance(duration, timedelta): return "00:00:00"
        ts = int(duration.total_seconds())
        return f"{ts // 3600:02d}:{(ts % 3600) // 60:02d}:{ts % 60:02d}"

    def _on_session_info_received(self, info: object):
        if not self.renderer or not self._is_running_flag: return
            
        foreground_aumid = self.monitor.get_foreground_window_aumid()
        foreground_proc_name = foreground_aumid.lower() if foreground_aumid else ""
        is_player_foreground = self.config.target_aumid.lower() in foreground_proc_name
        is_gui_foreground = (foreground_proc_name == self._self_proc_name)

        if is_player_foreground:
            self.renderer.show()
            self.renderer.set_stay_on_top(True)
        elif is_gui_foreground:
            self.renderer.show()
            self.renderer.set_stay_on_top(False)
        else:
            self.renderer.hide()
            return

        if not info or info.source_aumid != self.config.target_aumid:
            self.renderer.pause()
            if self.config.debug:
                 self.renderer.update_debug_playback_info("N/A", "00:00:00", "00:00:00")
            return
            
        if self.config.debug:
            pos_str = self._format_time_for_debug(info.position)
            dur_str = self._format_time_for_debug(info.duration)
            self.renderer.update_debug_playback_info(info.title, pos_str, dur_str)

        if info.status != "PLAYING":
            self.renderer.pause()
            return
        
        self.renderer.resume()
        current_position = info.position.total_seconds()
        
        if abs(current_position - self._last_known_position) > 2.0:
            logging.info(f"检测到播放跳转: {self._last_known_position:.1f}s -> {current_position:.1f}s，正在重置弹幕...")
            self.renderer.clear_danmaku()
            self._danmaku_idx = bisect.bisect_left(self.danmaku_start_times, current_position)

        self._last_known_position = current_position
        while (self._danmaku_idx < len(self.all_danmaku) and
               self.all_danmaku[self._danmaku_idx].start_time <= current_position):
            if self.renderer:
                self.renderer.add_danmaku(self.all_danmaku[self._danmaku_idx])
            self._danmaku_idx += 1
            
    def discover_sessions_for_ui(self):
        if not self.monitor:
            self.error_occurred.emit("媒体监控器不可用，无法发现会话。")
            return
        def discover_task():
            try:
                sessions = asyncio.run(self.monitor.list_sessions())
                self.sessions_discovered.emit(sessions)
            except Exception as e:
                logging.error(f"UI发现媒体会话时出错: {e}")
                self.error_occurred.emit(f"发现媒体会话时出错:\n{e}")
        thread = threading.Thread(target=discover_task, daemon=True)
        thread.start()