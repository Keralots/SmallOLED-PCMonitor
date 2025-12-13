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
import glob

# Configuration
ESP32_IP = "192.168.1.197"  # Change to your ESP32 IP address
UDP_PORT = 4210
BROADCAST_INTERVAL = 3  # Increased to 3 seconds for even less CPU usage

def get_linux_temperatures():
    """Get CPU and GPU temperatures and fan speed on Linux using psutil"""
    cpu_temp = None
    gpu_temp = None
    fan_speed = None

    # --- 1. Get Temperatures (CPU/GPU) ---
    if hasattr(psutil, "sensors_temperatures"):
        temps = psutil.sensors_temperatures()
        
        # CPU Temp Logic
        for key in temps:
            # Look for common CPU keys like 'coretemp', 'k10temp', 'cpu-thermal'
            if "coretemp" in key.lower() or "k10temp" in key.lower() or "cpu" in key.lower():
                for entry in temps[key]:
                    # Often the 'Package id 0' or 'Tctl' label has the main reading
                    if "package" in entry.label.lower() or "tctl" in entry.label.lower() or entry.label == "":
                        cpu_temp = int(entry.current)
                        break
                if cpu_temp is not None:
                    break

        # GPU Temp Logic (NVIDIA via psutil if available)
        for key in temps:
            if "nvidia" in key.lower() or "gpu" in key.lower():
                for entry in temps[key]:
                    gpu_temp = int(entry.current)
                    break
                if gpu_temp is not None:
                    break
                    
    # --- 2. Get Fan Speed ---
    if hasattr(psutil, "sensors_fans"):
        fans = psutil.sensors_fans()
        for key in fans:
            # Iterate through all fan entries and pick the first detected speed
            if fans[key]:
                # This usually picks up "fan1_input" or similar from the system
                fan_speed = int(fans[key][0].current)
                break
    
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
