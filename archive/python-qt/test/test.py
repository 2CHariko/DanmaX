# main.py
import asyncio
import time
from media_monitor import MediaMonitor, MediaMonitorError

# --- 配置 ---
# 在这里填入你想监控的程序的 AUMID。
# 你可以通过运行下面的 `discover_sessions` 函数来找到它。
# 常见示例: "PotPlayer64", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "chrome"
TARGET_AUMID = "PotPlayerMini64.exe" 

async def discover_sessions(monitor: MediaMonitor):
    """一个辅助函数，用于发现并打印所有可用的媒体会话。"""
    print("--- 正在发现所有活动媒体会话 ---")
    active_sessions = await monitor.list_sessions()
    if not active_sessions:
        print("未发现任何活动媒体会话。请打开一个播放器并播放媒体。")
    else:
        print("发现以下会话 (请将你需要的 AUMID 复制到上面的 TARGET_AUMID 变量中):")
        for sess in active_sessions:
            print(f"  - AUMID: {sess['aumid']:<50} | 标题: {sess['title']}")
    print("-------------------------------------\n")


async def main_loop():
    """主循环，演示如何使用 MediaMonitor 模块。"""
    try:
        monitor = MediaMonitor()
    except MediaMonitorError as e:
        print(f"初始化失败: {e}")
        return

    # 步骤1: 运行一次发现函数，帮助用户找到正确的 AUMID
    await discover_sessions(monitor)
    
    print(f"现在开始监控程序 '{TARGET_AUMID}' 的播放状态... (按 Ctrl+C 退出)")
    
    while True:
        try:
            # 步骤2: 使用筛选函数来获取特定程序的信息
            info = await monitor.get_filtered_session_info(TARGET_AUMID)
            
            if info:
                # 使用 SessionInfo 对象自带的格式化方法
                progress_str = info.format_time(info.position)
                duration_str = info.format_time(info.duration)
                
                output = (f"状态: {info.status:<10} | "
                          f"进度: {progress_str} / {duration_str} | "
                          f"标题: {info.title}")
            else:
                output = f"正在等待程序 '{TARGET_AUMID}' 播放媒体..."
            
            print(f"\r{output.ljust(100)}", end="")
            
            await asyncio.sleep(1)

        except KeyboardInterrupt:
            print("\n程序退出。")
            break
        except Exception as e:
            print(f"\n发生错误: {e}")
            break


if __name__ == "__main__":
    asyncio.run(main_loop())