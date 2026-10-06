# debug_overlay.py
import time
import os
import psutil
from collections import deque
from PyQt6.QtGui import QPainter, QColor, QPen, QFont
from PyQt6.QtCore import Qt, QRect

# 导入Config类仅用于类型注解
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from config_loader import Config

class DebugOverlay:
    """
    一个独立的调试信息覆盖层。
    负责在弹幕窗口上绘制FPS、内存占用、播放信息等。
    """
    def __init__(self, parent_window, config: 'Config', total_danmaku_count: int):
        self.parent = parent_window
        self.config = config
        self._paint_times = deque(maxlen=60)
        self._total_count = total_danmaku_count
        
        # 动态信息初始化
        self._active_count = 0
        self._pool_free = 0
        self._cpu_usage = 0.0
        self._mem_usage_mb = 0.0
        self._frame_count = 0
        
        # 播放器信息初始化
        self._media_title = "N/A"
        self._media_position = "00:00:00"
        self._media_duration = "00:00:00"
        
        self._font = QFont("Consolas", 8, QFont.Weight.Normal)
        
        try:
            self._proc = psutil.Process(os.getpid())
            self._proc.cpu_percent(interval=None)
        except psutil.NoSuchProcess:
            self._proc = None

        pos_map = {
            'top_left': Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft,
            'top_right': Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignRight,
            'bottom_right': Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight,
            'bottom_left': Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignLeft
        }
        self._alignment = pos_map.get(
            self.config.debug_info_position, 
            Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignLeft
        )

    def update_stats(self, active_count: int, pool_free: int):
        """从渲染器更新弹幕相关的统计数据。"""
        self._active_count = active_count
        self._pool_free = pool_free

    def update_playback_info(self, title: str, position_str: str, duration_str: str):
        """从渲染器更新播放器相关的统计数据。"""
        self._media_title = title if title else "N/A"
        self._media_position = position_str if position_str else "00:00:00"
        self._media_duration = duration_str if duration_str else "00:00:00"

    def _update_system_stats(self):
        """内部方法，用于更新CPU和内存等系统信息。"""
        if self._proc:
            self._cpu_usage = self._proc.cpu_percent(interval=None)
            self._mem_usage_mb = self._proc.memory_info().rss / (1024 * 1024)

    def paint(self, painter: QPainter):
        """
        执行实际的绘制操作，由父窗口的paintEvent调用。
        """
        # 绘制一个红色边框，明确标识调试模式已开启
        painter.setPen(QPen(QColor("red"), 2))
        painter.drawRect(self.parent.rect().adjusted(1, 1, -1, -1))
        
        # --- 节流更新系统信息 ---
        self._frame_count += 1
        if self._frame_count >= 30:
            self._frame_count = 0
            self._update_system_stats()
            
        # --- 计算FPS ---
        now = time.monotonic()
        self._paint_times.append(now)
        fps = 0
        if len(self._paint_times) > 1:
            elapsed = self._paint_times[-1] - self._paint_times[0]
            if elapsed > 0:
                fps = (len(self._paint_times) - 1) / elapsed
        
        # --- 组合所有调试信息 ---
        debug_text = (
            f"Title: {self._media_title}\n"
            f"Time: {self._media_position} / {self._media_duration}\n"
            f"--------------------------\n"
            f"FPS: {fps:.1f}\n"
            f"CPU: {self._cpu_usage:.1f}%\n"
            f"Mem: {self._mem_usage_mb:.1f} MB\n"
            f"Total Danmaku: {self._total_count}\n"
            f"Active Danmaku: {self._active_count}\n"
            f"Pool Free: {self._pool_free}"
        )
        
        painter.setFont(self._font)
        fm = painter.fontMetrics()
        
        # --- 计算绘制区域 ---
        margin = 15
        padding = 10
        
        text_bounding_rect = fm.boundingRect(QRect(0,0,0,0), Qt.AlignmentFlag.AlignLeft, debug_text)
        
        bg_width = text_bounding_rect.width() + 2 * padding
        bg_height = text_bounding_rect.height() + 2 * padding
        
        parent_rect = self.parent.rect()
        bg_x, bg_y = 0, 0
        
        if self._alignment & Qt.AlignmentFlag.AlignRight:
            bg_x = parent_rect.right() - margin - bg_width
        else:
            bg_x = parent_rect.left() + margin
            
        if self._alignment & Qt.AlignmentFlag.AlignBottom:
            bg_y = parent_rect.bottom() - margin - bg_height
        else:
            bg_y = parent_rect.top() + margin
            
        bg_rect = QRect(int(bg_x), int(bg_y), bg_width, bg_height)
        
        # --- 开始绘制 ---
        painter.save()
        
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(0, 0, 0, 150))
        painter.drawRoundedRect(bg_rect, 8, 8)
        
        painter.setPen(QColor("white"))
        text_draw_rect = bg_rect.adjusted(padding, padding, -padding, -padding)
        painter.drawText(text_draw_rect, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, debug_text)
        
        painter.restore()