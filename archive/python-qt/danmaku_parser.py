# danmaku_parser.py
import xml.etree.ElementTree as ET
from PyQt6.QtGui import QColor
from danmaku_models import DanmakuData
import logging

def load_from_xml(filepath: str) -> list[DanmakuData]:
    """
    从Bilibili风格的XML文件中加载、解析并排序弹幕。

    Args:
        filepath (str): XML弹幕文件的路径。

    Returns:
        list[DanmakuData]: 一个按开始时间排序的DanmakuData对象列表。
                          如果文件未找到或解析失败，返回空列表。
    """
    danmaku_list = []
    try:
        tree = ET.parse(filepath)
        root = tree.getroot()
        # 遍历XML中所有的 '<d>' 标签
        for d_element in root.findall('d'):
            # 'p' 属性包含了弹幕的多个参数，用逗号分隔
            p_attr = d_element.get('p', '').split(',')
            
            # 一个标准的B站弹幕p属性至少有8个字段，但我们只关心前4个
            if len(p_attr) >= 4:
                try:
                    # p_attr[0]: 弹幕出现时间 (秒)
                    start_time = float(p_attr[0])
                    # p_attr[1]: 弹幕模式 (1-3滚动, 4底部, 5顶部)
                    mode = int(p_attr[1])
                    # p_attr[3]: 颜色 (十进制整数表示的RGB)
                    color_decimal = int(p_attr[3])
                    # 弹幕文本内容
                    text = d_element.text
                    
                    # 只处理我们支持的模式，并且文本不能为空
                    if text and mode in [1, 4, 5]:
                        # 将十进制颜色值转换为QColor对象
                        # (dec >> 16) & 255: 右移16位取红色分量
                        # (dec >> 8) & 255: 右移8位取绿色分量
                        # dec & 255: 取蓝色分量
                        color = QColor((color_decimal >> 16) & 255, 
                                       (color_decimal >> 8) & 255, 
                                       color_decimal & 255)
                        danmaku_list.append(DanmakuData(start_time, mode, text, color))
                except (ValueError, IndexError) as e:
                    # 如果p属性中的某个值格式不正确（如无法转为数字），则忽略这条弹幕
                    logging.warning(f"忽略格式错误的弹幕行: p='{p_attr}', 错误: {e}")
                    continue
        
        # 【关键步骤】按开始时间对所有弹幕进行排序。
        # 这是后续使用二分查找进行同步的基础。
        danmaku_list.sort(key=lambda x: x.start_time)
        logging.info(f"成功加载 {len(danmaku_list)} 条有效弹幕。")
        return danmaku_list
        
    except FileNotFoundError:
        logging.error(f"错误: 弹幕文件 '{filepath}' 未找到。")
        return []
    except ET.ParseError as e:
        logging.error(f"解析XML时发生错误: {e}")
        return []
    except Exception as e:
        logging.error(f"加载弹幕时发生未知错误: {e}")
        return []