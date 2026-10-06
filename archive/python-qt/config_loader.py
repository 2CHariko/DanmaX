# config_loader.py
import logging
import configparser

class Config:
    """
    全局配置管理类，采用单例模式。
    负责加载、保存和提供对 'config.ini' 文件的访问。
    """
    _instance = None

    def __new__(cls, *args, **kwargs):
        """
        实现单例模式。确保全局只有一个Config实例。
        """
        if not cls._instance:
            cls._instance = super(Config, cls).__new__(cls)
        return cls._instance

    def __init__(self, filepath='config.ini'):
        """
        初始化配置类。
        使用 _initialized 标志防止重复初始化。
        
        Args:
            filepath (str): 配置文件的路径。
        """
        # 如果已经初始化过，则直接返回，避免重复加载
        if hasattr(self, '_initialized'):
            return
            
        self.filepath = filepath
        self.parser = configparser.ConfigParser()
        
        # 定义所有配置项的默认值，这使得程序在没有配置文件时也能正常运行
        self._defaults = {
            'Display': {
                'font_name': '微软雅黑', 'font_size': '24', 'stroke_width': '2',
                'max_tracks': '18', 'opacity': '0.85', 'line_spacing_ratio': '0.2'
            },
            'Danmaku': {
                'scroll_speed': '180', 'fixed_duration_ms': '5000', 
                'max_danmaku_count': '250',
                'allow_overlap': 'false' # 允许弹幕重叠
            },
            'Sync': {'target_aumid': 'PotPlayer64'},
            'Debug': {'enabled': 'false', 'info_position': 'bottom_left'},
            'OnTopStrategy': {'method': '1'},
            'Logging': {'level': 'INFO', 'log_to_file': 'true'},
            'DEFAULT': {'LastDanmakuPath': ''} # DEFAULT节用于存储全局默认值
        }
        self.load()
        self._initialized = True

    def load(self):
        """
        从 .ini 文件加载配置。
        它首先加载内置的默认值，然后用文件中的值覆盖它们。
        """
        parser = configparser.ConfigParser()
        # 1. 读取内置的默认值
        parser.read_dict(self._defaults)
        # 2. 读取文件，文件中的值会覆盖默认值
        parser.read(self.filepath, encoding='utf-8')
        self.parser = parser
        # 3. 将解析的值加载为类的属性，方便直接访问
        self._load_values()

    def restore_defaults(self):
        """
        将所有设置恢复为内置的默认值，并立即保存到文件。
        """
        logging.info("正在恢复所有设置为默认值...")
        parser = configparser.ConfigParser()
        parser.read_dict(self._defaults)
        self.parser = parser
        self._load_values()
        self.save()

    def _load_values(self):
        """
        私有方法，将 parser 中的配置项读取为强类型的类属性。
        """
        # [Display]
        self.font_name = self.parser.get('Display', 'font_name')
        self.font_size = self.parser.getint('Display', 'font_size')
        self.stroke_width = self.parser.getint('Display', 'stroke_width')
        self.max_tracks = self.parser.getint('Display', 'max_tracks')
        self.opacity = self.parser.getfloat('Display', 'opacity')
        self.line_spacing_ratio = self.parser.getfloat('Display', 'line_spacing_ratio')
        # [Danmaku]
        self.scroll_speed = self.parser.getint('Danmaku', 'scroll_speed')
        self.fixed_duration_ms = self.parser.getint('Danmaku', 'fixed_duration_ms')
        self.max_danmaku_count = self.parser.getint('Danmaku', 'max_danmaku_count')
        self.allow_overlap = self.parser.getboolean('Danmaku', 'allow_overlap')
        # [Sync] & [DEFAULT]
        self.target_aumid = self.parser.get('Sync', 'target_aumid')
        self.last_danmaku_path = self.parser.get('DEFAULT', 'LastDanmakuPath')
        # [Debug]
        self.debug = self.parser.getboolean('Debug', 'enabled')
        self.debug_info_position = self.parser.get('Debug', 'info_position')
        # [OnTopStrategy]
        self.ontop_strategy = self.parser.get('OnTopStrategy', 'method')
        # [Logging]
        self.log_level = self.parser.get('Logging', 'level')
        self.log_to_file = self.parser.getboolean('Logging', 'log_to_file')

    def save(self):
        """
        将当前内存中的配置值写回到 .ini 文件中。
        """
        # 更新 parser 对象中的值
        self.parser.set('Display', 'font_name', self.font_name)
        self.parser.set('Display', 'font_size', str(self.font_size))
        self.parser.set('Display', 'stroke_width', str(self.stroke_width))
        self.parser.set('Display', 'max_tracks', str(self.max_tracks))
        self.parser.set('Display', 'opacity', str(self.opacity))
        self.parser.set('Display', 'line_spacing_ratio', str(self.line_spacing_ratio))
        
        self.parser.set('Danmaku', 'scroll_speed', str(self.scroll_speed))
        self.parser.set('Danmaku', 'fixed_duration_ms', str(self.fixed_duration_ms))
        self.parser.set('Danmaku', 'max_danmaku_count', str(self.max_danmaku_count))
        self.parser.set('Danmaku', 'allow_overlap', str(self.allow_overlap).lower()) # bool转小写字符串
        
        self.parser.set('Sync', 'target_aumid', self.target_aumid)
        
        self.parser.set('Debug', 'enabled', str(self.debug).lower())
        self.parser.set('Debug', 'info_position', self.debug_info_position)
        
        self.parser.set('OnTopStrategy', 'method', self.ontop_strategy)
        
        self.parser.set('Logging', 'level', self.log_level)
        self.parser.set('Logging', 'log_to_file', str(self.log_to_file).lower())
        
        self.parser.set('DEFAULT', 'LastDanmakuPath', self.last_danmaku_path)
        
        try:
            with open(self.filepath, 'w', encoding='utf-8') as configfile:
                self.parser.write(configfile)
            logging.info(f"配置已成功保存到 {self.filepath}")
        except Exception as e:
            logging.error(f"保存配置失败: {e}")

def get_config():
    """
    全局访问点，用于获取Config的单例实例。
    """
    return Config()