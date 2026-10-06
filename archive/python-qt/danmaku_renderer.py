# danmaku_renderer.py
import logging
import time
import random
import sys
from collections import deque
from PyQt6.QtWidgets import QMainWindow, QApplication
from PyQt6.QtCore import Qt, QTimer, QPointF, QSize
from PyQt6.QtGui import (
    QFont, QPainter, QColor, QFontMetrics, QPainterPath,
    QPainterPathStroker, QPixmap
)

from config_loader import get_config
from danmaku_models import DanmakuData, ActiveDanmaku
from debug_overlay import DebugOverlay

# 平台相关的导入，使其成为可选
IS_WINDOWS = sys.platform == 'win32'
try:
    if IS_WINDOWS:
        import win32gui
        import win32con
    PYWIN32_AVAILABLE = True
except ImportError:
    PYWIN32_AVAILABLE = False


class DanmakuWindow(QMainWindow):
    """
    弹幕渲染窗口。这是一个透明、无边框、可鼠标穿透的顶层窗口。
    它负责管理所有活动弹幕的生命周期、动画更新和绘制。
    """
    def __init__(self, total_danmaku_count: int, parent=None):
        super().__init__(parent)
        self.config = get_config()
        
        self.config.screen_geometry = QApplication.primaryScreen().geometry()
        
        window_flags = Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool
        self.setWindowFlags(window_flags)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        if not self.config.debug:
            self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        
        self.setGeometry(self.config.screen_geometry)
        
        self._font = QFont(self.config.font_name, self.config.font_size, QFont.Weight.Bold)
        self._font_metrics = QFontMetrics(self._font)
        
        pool_size = self.config.max_danmaku_count
        logging.info(f"初始化对象池大小: {pool_size}")
        self._danmaku_pool = [ActiveDanmaku() for _ in range(pool_size)]
        self._free_danmaku = deque(self._danmaku_pool)
        self._active_danmaku = []
        
        font_height = self._font_metrics.height()
        line_spacing = int(font_height * self.config.line_spacing_ratio)
        self.track_height = font_height + line_spacing
        self.y_offset = self._font_metrics.ascent() + 5
        
        num_tracks = self.config.max_tracks
        self._scroll_tracks = [0] * num_tracks
        self._top_tracks = [0] * num_tracks
        self._bottom_tracks = [0] * num_tracks
        
        if self.config.debug:
            self.debug_overlay = DebugOverlay(self, self.config, total_danmaku_count)
        else:
            self.debug_overlay = None
            
        self._animation_timer = QTimer(self)
        self._animation_timer.timeout.connect(self.update_states)
        self._animation_timer.start(1000 // 60)
        
        self._on_top_timer = QTimer(self)
        self._on_top_timer.timeout.connect(self._force_on_top_win32_if_needed)

    def update_debug_playback_info(self, title: str, position_str: str, duration_str: str):
        """【新】将播放器信息传递给调试层。"""
        if self.debug_overlay:
            self.debug_overlay.update_playback_info(title, position_str, duration_str)

    # ... (其余方法 _find_track, set_stay_on_top, add_danmaku, 等保持不变) ...
    def _find_track(self, danmaku_data: DanmakuData, text_width: int) -> tuple[float, bool]:
        if self.config.allow_overlap:
            return self._find_track_with_overlap(danmaku_data)
        else:
            return self._find_track_without_overlap(danmaku_data, text_width)

    def _find_track_with_overlap(self, danmaku_data: DanmakuData) -> tuple[float, bool]:
        mode = danmaku_data.mode
        num_tracks = self.config.max_tracks
        if num_tracks <= 0: return 0, False
        track_idx = random.randint(0, num_tracks - 1)
        if mode == 1 or mode == 5:
            y_pos = (track_idx * self.track_height) + self.y_offset
            return y_pos, True
        elif mode == 4:
            y_pos = self.height() - ((track_idx + 1) * self.track_height)
            return y_pos, True
        return 0, False

    def _find_track_without_overlap(self, danmaku_data: DanmakuData, text_width: int) -> tuple[float, bool]:
        current_time = time.monotonic()
        mode = danmaku_data.mode
        if mode == 1:
            available_tracks = [i for i, t in enumerate(self._scroll_tracks) if current_time > t]
            if not available_tracks: return 0, False
            track_idx = random.choice(available_tracks)
            y_pos = (track_idx * self.track_height) + self.y_offset
            self._scroll_tracks[track_idx] = current_time + (text_width / self.config.scroll_speed) * 0.8
            return y_pos, True
        elif mode == 5:
            for i, track_time in enumerate(self._top_tracks):
                if current_time > track_time:
                    y_pos = (i * self.track_height) + self.y_offset
                    self._top_tracks[i] = current_time + (self.config.fixed_duration_ms / 1000)
                    return y_pos, True
            return 0, False
        elif mode == 4:
            for i, track_time in enumerate(self._bottom_tracks):
                if current_time > track_time:
                    y_pos = self.height() - ((i + 1) * self.track_height)
                    self._bottom_tracks[i] = current_time + (self.config.fixed_duration_ms / 1000)
                    return y_pos, True
            return 0, False
        return 0, False

    def set_stay_on_top(self, stay_on_top: bool):
        if IS_WINDOWS and stay_on_top and int(self.config.ontop_strategy) > 1:
            if not self._on_top_timer.isActive():
                self._on_top_timer.start(2000)
        else:
            if self._on_top_timer.isActive():
                self._on_top_timer.stop()
        current_on_top_flag = bool(self.windowFlags() & Qt.WindowType.WindowStaysOnTopHint)
        if current_on_top_flag == stay_on_top: return
        self.setWindowFlag(Qt.WindowType.WindowStaysOnTopHint, stay_on_top)
        self.show()

    def _force_on_top_win32_if_needed(self):
        if not IS_WINDOWS or not PYWIN32_AVAILABLE: return
        strategy = int(self.config.ontop_strategy)
        if strategy < 2: return
        try:
            hwnd = int(self.winId())
            win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, 0,0,0,0, win32con.SWP_NOMOVE | win32con.SWP_NOSIZE | win32con.SWP_NOACTIVATE)
        except Exception as e:
            logging.error(f"Win32 on-top error: {e}")
            self._on_top_timer.stop()

    def add_danmaku(self, danmaku_data: DanmakuData):
        if not self._free_danmaku:
            logging.warning("对象池已满，无法添加新弹幕。")
            return
        text_width = self._font_metrics.horizontalAdvance(danmaku_data.text)
        y_pos, track_found = self._find_track(danmaku_data, text_width)
        if not track_found: return
        danmaku_obj = self._free_danmaku.popleft()
        danmaku_obj.init(danmaku_data, y_pos, text_width, self.config)
        self._active_danmaku.append(danmaku_obj)

    def update_states(self):
        delta_time = 1 / 60.0
        current_time = time.monotonic()
        still_active = []
        for d in self._active_danmaku:
            if d.is_active(current_time, delta_time):
                still_active.append(d)
            else:
                self._free_danmaku.append(d)
        self._active_danmaku = still_active
        if self.debug_overlay:
            self.debug_overlay.update_stats(
                active_count=len(self._active_danmaku),
                pool_free=len(self._free_danmaku)
            )
        self.update()

    def _render_danmaku_to_pixmap(self, danmaku: ActiveDanmaku):
        stroke_offset = self.config.stroke_width
        bounding_rect = self._font_metrics.boundingRect(danmaku.text)
        pixmap_size = QSize(bounding_rect.width() + stroke_offset * 2, bounding_rect.height() + stroke_offset * 2)
        pixmap = QPixmap(pixmap_size)
        pixmap.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setFont(self._font)
        path = QPainterPath()
        path.addText(stroke_offset, self._font_metrics.ascent() + stroke_offset, self._font, danmaku.text)
        if self.config.stroke_width > 0:
            stroker = QPainterPathStroker()
            stroker.setWidth(self.config.stroke_width * 2)
            stroker.setCapStyle(Qt.PenCapStyle.RoundCap)
            stroker.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
            stroke_path = stroker.createStroke(path)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.fillPath(stroke_path, QColor("black"))
        painter.fillPath(path, danmaku.color)
        painter.end()
        danmaku.pixmap_cache = pixmap

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setOpacity(self.config.opacity)
        for danmaku in self._active_danmaku:
            if danmaku.pixmap_cache is None:
                self._render_danmaku_to_pixmap(danmaku)
            if danmaku.pixmap_cache:
                draw_pos = QPointF(danmaku.position.x() - self.config.stroke_width,
                                   danmaku.position.y() - self.config.stroke_width - self._font_metrics.ascent())
                painter.drawPixmap(draw_pos, danmaku.pixmap_cache)
        painter.setOpacity(1.0)
        if self.debug_overlay:
            self.debug_overlay.paint(painter)

    def clear_danmaku(self):
        self._free_danmaku.extend(self._active_danmaku)
        self._active_danmaku.clear()
        self.update()

    def pause(self):
        if self._animation_timer.isActive():
            self._animation_timer.stop()

    def resume(self):
        if not self._animation_timer.isActive():
            self._animation_timer.start(1000 // 60)