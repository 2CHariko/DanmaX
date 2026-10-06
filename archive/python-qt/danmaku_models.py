# danmaku_models.py
import time
from PyQt6.QtCore import QPointF
from PyQt6.QtGui import QColor, QPixmap

# 导入Config类仅用于类型注解，避免在运行时发生循环导入
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from config_loader import Config


class DanmakuData:
    """
    存储从XML文件解析出的原始、静态的弹幕数据。
    这是一个纯数据类（DTO - Data Transfer Object），在程序运行期间其属性不会改变。
    """
    def __init__(self, start_time: float, mode: int, text: str, color: QColor):
        self.start_time = start_time  # 弹幕出现的时间（秒）
        self.mode = mode              # 弹幕模式 (1=滚动, 4=底部, 5=顶部)
        self.text = text              # 弹幕文本
        self.color = color            # 弹幕颜色 (QColor对象)

class ActiveDanmaku:
    """
    代表一条当前正在屏幕上显示或运动的“活动”弹幕。
    它的属性是动态变化的（如位置），并且包含了渲染所需的缓存。
    这个类被设计为可复用的（通过对象池）。
    """
    def __init__(self):
        """初始化一个空的活动弹幕对象。"""
        self.text: str = ""
        self.color: QColor = QColor()
        self.mode: int = 0
        self.width: int = 0             # 弹幕文本渲染后的像素宽度
        self.position: QPointF = QPointF() # 弹幕当前的左上角坐标
        self.speed: float = 0.0         # 弹幕的移动速度（像素/秒），仅滚动弹幕有效
        self.disappear_time: float = 0.0 # 弹幕应消失的绝对时间戳，仅固定弹幕有效
        
        # 【性能优化】用于缓存渲染好的弹幕图片（包含描边）。
        # 避免每一帧都重新绘制文字，极大提升性能。
        self.pixmap_cache: QPixmap | None = None

    def init(self, data: DanmakuData, y_pos: float, width: int, config: 'Config'):
        """
        使用一条静态弹幕数据来“激活”这个对象。
        这个方法在从对象池取出对象后被调用。

        Args:
            data (DanmakuData): 原始弹幕数据。
            y_pos (float): 分配到的弹幕轨道的y坐标。
            width (int): 预先计算好的弹幕文本宽度。
            config (Config): 全局配置对象。
        """
        self.text = data.text
        self.color = data.color
        self.mode = data.mode
        self.width = width
        # 重置缓存，因为弹幕内容已经改变
        self.pixmap_cache = None
        
        # 获取屏幕宽度用于计算初始位置
        screen_width = config.screen_geometry.width()

        # 根据弹幕模式设置初始位置、速度和消失时间
        if self.mode == 1:  # 滚动弹幕
            self.position = QPointF(screen_width, y_pos) # 初始位置在屏幕右侧外
            self.speed = config.scroll_speed
            self.disappear_time = float('inf') # 滚动弹幕永不因时间消失，只因移出屏幕
        else:  # 顶部或底部固定弹幕
            # 初始位置在屏幕中央
            self.position = QPointF((screen_width - width) / 2, y_pos)
            self.speed = 0 # 固定弹幕不移动
            current_time = time.monotonic() # 使用单调时钟，不受系统时间改变影响
            # 计算绝对消失时间
            self.disappear_time = current_time + (config.fixed_duration_ms / 1000)

    def is_active(self, current_time: float, delta_time: float) -> bool:
        """
        判断弹幕在当前帧是否仍然处于活动状态，并更新其位置。
        这是弹幕动画的核心逻辑。

        Args:
            current_time (float): 当前时间戳 (来自 time.monotonic())。
            delta_time (float): 距离上一帧的时间差（秒）。

        Returns:
            bool: 如果弹幕仍然活动（在屏幕上可见），返回True，否则返回False。
        """
        if self.mode == 1:  # 滚动弹幕
            # 根据时间差和速度更新x坐标
            self.position.setX(self.position.x() - self.speed * delta_time)
            # 如果弹幕的右边缘仍在屏幕左侧之外，则认为它还在活动
            return self.position.x() + self.width > 0
        else:  # 固定弹幕
            # 如果当前时间还没到预设的消失时间，则为活动
            return current_time < self.disappear_time