# test_overlay.py
import sys
import random
from PyQt6.QtWidgets import QApplication, QMainWindow, QLabel
from PyQt6.QtCore import Qt, QTimer, QPropertyAnimation, QPoint, QRect
from PyQt6.QtGui import QColor, QScreen

# --- 配置区 ---
TEST_DANMAKU = [
    "这是一条测试弹幕 (●'◡'●)",
    "Hello World from PyQt6!",
    "弹幕正在滚动...",
    "6666666666666",
    "Python is awesome!",
    "这是一个全屏悬浮窗测试",
    "文字可以设置不同颜色",
    "Animation Test",
    "滚动速度可以调整",
]

# 弹幕产生的最小和最大时间间隔（毫秒）
SPAWN_INTERVAL_MIN = 300
SPAWN_INTERVAL_MAX = 1200

# 弹幕滚动的最小和最大速度（毫秒）
DANMAKU_SPEED_MIN = 8000
DANMAKU_SPEED_MAX = 12000

# 弹幕字体大小
DANMAKU_FONT_SIZE = 24


class DanmakuOverlay(QMainWindow):
    """
    一个全屏、透明、可穿透的弹幕悬浮窗。
    """
    def __init__(self):
        super().__init__()
        
        # 1. 设置窗口的核心属性，使其成为一个悬浮窗
        self.setWindowFlags(
            Qt.WindowType.FramelessWindowHint |        # 无边框
            Qt.WindowType.WindowStaysOnTopHint |       # 始终置顶
            Qt.WindowType.Tool                         # 不在任务栏显示图标
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground) # 背景透明
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents) # 鼠标穿透

        # 2. 设置窗口为全屏大小
        screen_geometry = QScreen.availableGeometry(QApplication.primaryScreen())
        self.setGeometry(screen_geometry)

        # 3. 用于存储正在运行的动画，防止被垃圾回收
        self._animations = []

        # 4. 设置一个定时器，周期性地产生新的弹幕
        self._spawn_timer = QTimer(self)
        self._spawn_timer.timeout.connect(self._spawn_danmaku)
        
        print("悬浮窗已创建。按 Ctrl+C 在终端中关闭。")

    def start_danmaku(self):
        """开始产生弹幕"""
        print("开始发送弹幕...")
        self._spawn_timer.start(random.randint(SPAWN_INTERVAL_MIN, SPAWN_INTERVAL_MAX))

    def _spawn_danmaku(self):
        """产生并显示一条新的弹幕"""
        # 随机选择弹幕文本和颜色
        text = random.choice(TEST_DANMAKU)
        color = QColor(random.randint(128, 255), random.randint(128, 255), random.randint(128, 255))

        # 创建一个 QLabel 来显示弹幕
        danmaku_label = QLabel(text, self)
        danmaku_label.setStyleSheet(
            f"color: {color.name()};"
            f"font-size: {DANMAKU_FONT_SIZE}px;"
            f"font-weight: bold;"
            # 添加一点描边效果，让文字在任何背景下都更清晰
            "text-shadow: -1px -1px 0 #000, 1px -1px 0 #000, -1px 1px 0 #000, 1px 1px 0 #000;"
        )
        danmaku_label.adjustSize() # 根据内容调整大小

        # 随机选择一个弹道（y坐标），确保弹幕完整显示在屏幕内
        start_y = random.randint(0, self.height() - danmaku_label.height())
        
        # 设置弹幕的初始位置在窗口最右侧之外
        start_pos = QPoint(self.width(), start_y)
        danmaku_label.move(start_pos)
        danmaku_label.show()

        # 创建属性动画，让弹幕从右向左移动
        animation = QPropertyAnimation(danmaku_label, b"pos", self)
        self._animations.append(animation) # 将动画对象保存起来
        
        # 设置动画时长（即弹幕速度）
        duration = random.randint(DANMAKU_SPEED_MIN, DANMAKU_SPEED_MAX)
        animation.setDuration(duration)

        # 设置动画的起始和结束位置
        animation.setStartValue(start_pos)
        animation.setEndValue(QPoint(-danmaku_label.width(), start_y)) # 结束位置在最左侧之外

        # 关键一步：当动画完成后，自动销毁弹幕 QLabel 和动画本身，释放资源
        animation.finished.connect(lambda: self._cleanup_animation(danmaku_label, animation))
        
        animation.start()
        
        # 重置定时器，使用下一次随机的间隔
        self._spawn_timer.setInterval(random.randint(SPAWN_INTERVAL_MIN, SPAWN_INTERVAL_MAX))

    def _cleanup_animation(self, label, animation):
        """动画完成后的清理工作"""
        label.deleteLater()
        if animation in self._animations:
            self._animations.remove(animation)


def main():
    try:
        app = QApplication(sys.argv)
        
        overlay = DanmakuOverlay()
        overlay.show()
        overlay.start_danmaku()
        
        sys.exit(app.exec())
    except KeyboardInterrupt:
        print("脚本被用户中断。")
    except Exception as e:
        print(f"发生错误: {e}")

if __name__ == "__main__":
    main()