"""
PC Stats Monitor - Ultra Optimized Version
Uses sensor identifiers for direct access - minimal CPU usage
"""

import psutil
import socket
import time
import json
from datetime import datetime

# Configuration
ESP32_IP = "192.168.0.163"  # Change to your ESP32 IP address
UDP_PORT = 4210
BROADCAST_INTERVAL = 3  # Increased to 3 seconds for even less CPU usage

# Sensor identifiers (will be populated at startup)
sensor_ids = {
    'pump': None,
    'cpu_temp': None,
    'gpu_temp': None
}

def initialize_sensors():
    """Find sensor identifiers once at startup"""
    print("Scanning for sensors...")
    
    try:
        import wmi
        w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
        sensors = w.Sensor()
        
        # Find our specific sensors and save their identifiers
        for sensor in sensors:
            if sensor.SensorType == 'Fan' and sensor.Name == 'Pump Fan':
                sensor_ids['pump'] = sensor.Identifier
                print(f"✓ Pump Fan ID: {sensor.Identifier}")
            
            elif sensor.SensorType == 'Temperature' and sensor.Name == 'CPU Package':
                sensor_ids['cpu_temp'] = sensor.Identifier
                print(f"✓ CPU Temp ID: {sensor.Identifier}")
            
            elif sensor.SensorType == 'Temperature' and sensor.Name == 'GPU Core':
                sensor_ids['gpu_temp'] = sensor.Identifier
                print(f"✓ GPU Temp ID: {sensor.Identifier}")
        
        return True
        
    except Exception as e:
        print(f"✗ Error: {e}")
        return False

def get_sensor_value(w, identifier):
    """Get single sensor value by identifier - very fast!"""
    try:
        # Query only the specific sensor we want
        sensor = w.Sensor(Identifier=identifier)[0]
        return int(sensor.Value)
    except:
        return None

def get_sensor_values():
    """Get values from specific sensors only - minimal WMI queries"""
    try:
        import wmi
        w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
        
        # Get only the sensors we need - no iteration through hundreds
        pump_speed = get_sensor_value(w, sensor_ids['pump']) if sensor_ids['pump'] else None
        cpu_temp = get_sensor_value(w, sensor_ids['cpu_temp']) if sensor_ids['cpu_temp'] else None
        gpu_temp = get_sensor_value(w, sensor_ids['gpu_temp']) if sensor_ids['gpu_temp'] else None
        
        return pump_speed, cpu_temp, gpu_temp
        
    except Exception as e:
        return None, None, None

def get_system_stats():
    """Collect system statistics"""
    # Get hardware sensors
    pump_speed, cpu_temp, gpu_temp = get_sensor_values()
    
    # Get system stats (minimal CPU usage)
    stats = {
        'timestamp': datetime.now().strftime('%H:%M'),
        'cpu_percent': round(psutil.cpu_percent(interval=0), 1),  # Non-blocking
        'ram_percent': round(psutil.virtual_memory().percent, 1),
        'ram_used_gb': round(psutil.virtual_memory().used / (1024**3), 1),
        'ram_total_gb': round(psutil.virtual_memory().total / (1024**3), 1),
        'disk_percent': round(psutil.disk_usage('C:\\').percent, 1),
        'cpu_temp': cpu_temp,
        'gpu_temp': gpu_temp,
        'fan_speed': pump_speed,
        'status': 'online'
    }
    return stats

def send_stats(sock, stats):
    """Send stats to ESP32 via UDP"""
    try:
        message = json.dumps(stats).encode('utf-8')
        sock.sendto(message, (ESP32_IP, UDP_PORT))
        print(f"[{stats['timestamp']}] CPU {stats['cpu_percent']}% | RAM {stats['ram_percent']}% | Pump {stats['fan_speed'] or 'N/A'}")
    except Exception as e:
        print(f"Error: {e}")

def main():
    """Main loop"""
    print("=" * 60)
    print("PC Stats Monitor - Ultra Optimized")
    print("=" * 60)
    print(f"ESP32: {ESP32_IP}:{UDP_PORT}")
    print(f"Update: Every {BROADCAST_INTERVAL}s")
    print("-" * 60)
    
    if not initialize_sensors():
        print("⚠ Sensor initialization failed")
        input("Press Enter to exit...")
        return
    
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