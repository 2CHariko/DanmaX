# main.py
import sys
import logging
from PyQt6.QtWidgets import QApplication

from config_loader import get_config
from control_panel import MainWindow
from danmaku_controller import DanmakuController
from logger_setup import setup_logging

def main():
    """
    应用程序主入口函数。
    """
    # 初始化Qt应用程序实例
    app = QApplication(sys.argv)
    
    # 获取全局单例配置对象
    config = get_config()
    
    # 设置全局日志系统，并获取用于连接GUI的信号
    log_signals = setup_logging(config)

    # 创建主窗口（控制面板），并传入日志信号
    main_window = MainWindow(log_signals)
    
    # 创建弹幕控制器，这是应用的核心逻辑处理单元
    # 注意：控制器现在内部会根据平台选择合适的媒体监控器
    controller = DanmakuController() 
    
    # 将控制器与主窗口关联，使得UI可以调用控制器的功能
    main_window.set_controller(controller)
    
    # 显示主窗口
    main_window.show()
    
    logging.info("控制面板启动成功。")
    
    # 进入Qt应用程序的事件循环
    sys.exit(app.exec())

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        # 捕获所有未被处理的顶层异常，记录后退出
        # 这是保证程序健壮性的最后一道防线
        logging.critical("应用程序发生未捕获的严重错误: %s", e, exc_info=True)
        sys.exit(1)