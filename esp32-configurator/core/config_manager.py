"""
ESP32 Configurator - Configuration Manager
Handles saving/loading configuration to/from JSON files
"""

import json
import os
from typing import Dict, Tuple


class ConfigManager:
    """
    Manages configuration save/load operations

    Config format matches monitor_config_hwinfo.json
    """

    DEFAULT_CONFIG_PATH = "esp32_config.json"

    @staticmethod
    def save_config(file_path: str, settings: Dict, metrics: list) -> Tuple[bool, str]:
        """
        Save configuration to JSON file

        Args:
            file_path: Path to save file
            settings: Settings dict (esp32_ip, udp_port, update_interval)
            metrics: List of metric dicts

        Returns:
            (success, message)
        """
        try:
            # Build config dict
            config = {
                "esp32_ip": settings.get("esp32_ip", "192.168.0.163"),
                "udp_port": settings.get("udp_port", 4210),
                "update_interval": settings.get("update_interval", 3),
                "metrics": metrics
            }

            # Write to file
            with open(file_path, 'w') as f:
                json.dump(config, f, indent=2)

            return True, f"Configuration saved to {file_path}"

        except Exception as e:
            return False, f"Failed to save configuration: {str(e)}"

    @staticmethod
    def load_config(file_path: str) -> Tuple[bool, Dict, str]:
        """
        Load configuration from JSON file

        Args:
            file_path: Path to config file

        Returns:
            (success, config_dict, message)
        """
        try:
            # Check if file exists
            if not os.path.exists(file_path):
                return False, {}, f"File not found: {file_path}"

            # Read file
            with open(file_path, 'r') as f:
                config = json.load(f)

            # Validate config
            if not isinstance(config, dict):
                return False, {}, "Invalid configuration format"

            # Validate required fields
            required_fields = ["esp32_ip", "udp_port", "update_interval", "metrics"]
            for field in required_fields:
                if field not in config:
                    return False, {}, f"Missing required field: {field}"

            # Validate metrics
            if not isinstance(config["metrics"], list):
                return False, {}, "Invalid metrics format"

            return True, config, f"Configuration loaded from {file_path}"

        except json.JSONDecodeError as e:
            return False, {}, f"Invalid JSON format: {str(e)}"
        except Exception as e:
            return False, {}, f"Failed to load configuration: {str(e)}"

    @staticmethod
    def export_config(file_path: str, settings: Dict, metrics: list) -> Tuple[bool, str]:
        """
        Export configuration to user-specified file

        Args:
            file_path: Path to export file
            settings: Settings dict
            metrics: List of metric dicts

        Returns:
            (success, message)
        """
        return ConfigManager.save_config(file_path, settings, metrics)

    @staticmethod
    def import_config(file_path: str) -> Tuple[bool, Dict, str]:
        """
        Import configuration from user-specified file

        Args:
            file_path: Path to import file

        Returns:
            (success, config_dict, message)
        """
        return ConfigManager.load_config(file_path)

    @staticmethod
    def get_default_config() -> Dict:
        """
        Get default configuration

        Returns:
            Default config dict
        """
        return {
            "esp32_ip": "192.168.0.163",
            "udp_port": 4210,
            "update_interval": 3,
            "metrics": []
        }

    @staticmethod
    def validate_metric(metric: Dict) -> bool:
        """
        Validate a metric dict

        Args:
            metric: Metric dict to validate

        Returns:
            True if valid, False otherwise
        """
        required_fields = ["id", "name", "display_name", "source", "type", "unit"]

        for field in required_fields:
            if field not in metric:
                return False

        # Validate source-specific fields
        source = metric.get("source")
        if source == "hwinfo":
            if "hwinfo_reading_id" not in metric:
                return False
        elif source == "wmi":
            if "wmi_identifier" not in metric:
                return False

        return True
