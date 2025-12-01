"""
PC Stats Monitor - Ultra Optimized Version
Uses sensor identifiers for direct access - minimal CPU usage
"""

import psutil
import socket
import time
import json
from datetime import datetime
import platform

# Configuration
ESP32_IP = "192.168.1.197"  # Change to your ESP32 IP address
UDP_PORT = 4210
BROADCAST_INTERVAL = 3  # Increased to 3 seconds for even less CPU usage



def get_linux_temperatures():
    """Get CPU and GPU temperatures on Linux using psutil and /sys/class/thermal"""
    cpu_temp = None
    gpu_temp = None
    fan_speed = None

    # Try psutil.sensors_temperatures()
    if hasattr(psutil, "sensors_temperatures"):
        temps = psutil.sensors_temperatures()
        # CPU
        for key in temps:
            if "coretemp" in key or "cpu" in key or 'k10temp' in key:
                for entry in temps[key]:
                    if entry.label.lower() in ["package id 0", "core 0", "cpu", "tctl"] or entry.label == "":
                        cpu_temp = int(entry.current)
                        break
            # GPU (NVIDIA)
            if "nvidia" in key or "gpu" in key:
                for entry in temps[key]:
                    gpu_temp = int(entry.current)
                    break
    # Try reading fan speed from /sys/class/hwmon
    try:
        import glob
        for hwmon in glob.glob("/sys/class/hwmon/hwmon*/fan*_input"):
            with open(hwmon) as f:
                fan_speed = int(f.read().strip())
                break
    except Exception:
        pass
    return fan_speed, cpu_temp, gpu_temp

def get_sensor_values():
    """Get sensor values for Linux (CPU temp, GPU temp, fan speed)"""
    if platform.system() == "Linux":
        return get_linux_temperatures()
    else:
        # Not supported
        return None, None, None

def get_system_stats():
    """Collect system statistics (Linux compatible)"""
    fan_speed, cpu_temp, gpu_temp = get_sensor_values()
    stats = {
        'timestamp': datetime.now().strftime('%H:%M'),
        'cpu_percent': round(psutil.cpu_percent(interval=0), 1),
        'ram_percent': round(psutil.virtual_memory().percent, 1),
        'ram_used_gb': round(psutil.virtual_memory().used / (1024**3), 1),
        'ram_total_gb': round(psutil.virtual_memory().total / (1024**3), 1),
        'disk_percent': round(psutil.disk_usage('/').percent, 1),
        'cpu_temp': cpu_temp,
        'gpu_temp': gpu_temp,
        'fan_speed': fan_speed,
        'status': 'online'
    }
    return stats

def send_stats(sock, stats):
    """Send stats to ESP32 via UDP"""
    try:
        message = json.dumps(stats).encode('utf-8')
        sock.sendto(message, (ESP32_IP, UDP_PORT))
        print(f"[{stats['timestamp']}] CPU {stats['cpu_percent']}% | RAM {stats['ram_percent']}% | Fan {stats['fan_speed'] or 'N/A'} RPM")
    except Exception as e:
        print(f"Error: {e}")

def main():
    """Main loop (Linux compatible)"""
    print("=" * 60)
    print("PC Stats Monitor - Linux Edition")
    print("=" * 60)
    print(f"ESP32: {ESP32_IP}:{UDP_PORT}")
    print(f"Update: Every {BROADCAST_INTERVAL}s")
    print("-" * 60)
    print("Monitoring... (Ctrl+C to stop)")
    print("-" * 60)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Initial CPU reading to warm up psutil
    psutil.cpu_percent(interval=1)

    try:
        while True:
            stats = get_system_stats()
            send_stats(sock, stats)
            time.sleep(BROADCAST_INTERVAL)
    except KeyboardInterrupt:
        print("\n\nStopped.")
    finally:
        sock.close()

if __name__ == "__main__":
    main()