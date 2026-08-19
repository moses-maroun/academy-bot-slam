#!/usr/bin/env python3
"""courier.launch.py — run the courier dispatcher and executor nodes."""
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg = get_package_share_directory('acadbot_courier')
    locations = os.path.join(pkg, 'config', 'locations.yaml')
    params = os.path.join(pkg, 'config', 'courier_params.yaml')

    return LaunchDescription([
        Node(
            package='acadbot_courier',
            executable='courier_dispatcher',
            name='courier_dispatcher',
            output='screen',
            parameters=[locations, params],
        ),
        Node(
            package='acadbot_courier',
            executable='courier_executor',
            name='courier_executor',
            output='screen',
            parameters=[locations, params],
        ),
    ])
