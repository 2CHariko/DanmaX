# logger_setup.py
import logging
import os
import sys
from datetime import datetime
from PyQt6.QtCore import QObject, pyqtSignal

# 导入Config类仅用于类型注解
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from config_loader import Config

# 定义一个包含Qt信号的QObject，用于日志系统和GUI之间的通信
class LogSignals(QObject):
    # 当有新日志时，会发射此信号，并携带格式化后的日志字符串
    log_message = pyqtSignal(str)

# 自定义日志处理器，继承自 logging.Handler
class QtLogHandler(logging.Handler):
    """
    一个自定义的日志处理器，它将日志记录通过Qt信号发送出去。
    这使得logging模块的输出可以被重定向到GUI组件（如QPlainTextEdit）。
    """
    def __init__(self, signals: LogSignals):
        super().__init__()
        self.signals = signals

    def emit(self, record):
        """
        当一条日志记录需要被处理时，此方法会被调用。
        它会格式化记录，然后通过信号发射出去。
        """
        try:
            # format()方法会使用setFormatter设置的格式化器来格式化日志记录
            msg = self.format(record)
            self.signals.log_message.emit(msg)
        except Exception:
            self.handleError(record)

def setup_logging(config: 'Config'):
    """
    配置全局日志系统。

    这个函数会设置三种日志输出目标：
    1. 命令行 (stdout)
    2. GUI界面 (通过Qt信号)
    3. 文件 (如果配置中启用)

    Args:
        config (Config): 全局配置对象。

    Returns:
        LogSignals: 包含信号的对象，需要将其连接到GUI的槽函数上。
    """
    # 从配置中读取日志级别，并转换为logging模块对应的常量
    log_level_str = config.log_level.upper()
    log_level = getattr(logging, log_level_str, logging.INFO)
    
    # 定义全局统一的日志格式
    log_formatter = logging.Formatter('%(asctime)s [%(levelname)-8s] %(message)s', datefmt='%H:%M:%S')

    # 获取根logger，配置它会影响到所有未单独配置的子logger
    root_logger = logging.getLogger()
    root_logger.setLevel(log_level)
    
    # 清除任何可能存在的旧处理器，以防重复加载
    root_logger.handlers.clear()

    # 1. 配置命令行输出 (StreamHandler)
    stream_handler = logging.StreamHandler(sys.stdout)
    stream_handler.setFormatter(log_formatter)
    root_logger.addHandler(stream_handler)

    # 2. 配置GUI输出 (通过自定义的QtLogHandler)
    log_signals = LogSignals()
    gui_handler = QtLogHandler(log_signals)
    gui_handler.setFormatter(log_formatter)
    root_logger.addHandler(gui_handler)

    # 3. 配置可选的文件输出 (FileHandler)
    if config.log_to_file:
        log_dir = "logs"
        os.makedirs(log_dir, exist_ok=True)
        # 使用时间戳命名日志文件，避免覆盖
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        log_file = os.path.join(log_dir, f"{timestamp}.log")
        
        file_handler = logging.FileHandler(log_file, encoding='utf-8')
        file_handler.setFormatter(log_formatter)
        root_logger.addHandler(file_handler)
        
        logging.info(f"日志将保存到: {log_file}")

    logging.info(f"日志系统初始化完成，级别: {log_level_str}")
    # 返回log_signals对象，以便主程序可以连接它的信号
    return log_signals