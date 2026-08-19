#!/usr/bin/env python3
"""courier_demo.launch.py — one command for the final project courier demo.

Brings up:  simulation + AMCL localization + Nav2 (via autonomy.launch.py)
plus the courier_dispatcher and courier_executor nodes (via
acadbot_courier's courier.launch.py).

    ros2 launch acadbot_bringup courier_demo.launch.py

Then, once the stack is up (costmaps painting in RViz):

    ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
      "{pickup: reception, dropoff: lab_bench}"
    ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
      "{job_id: job_1}" --feedback

Arguments:
    localization:=amcl|slam   which localizer to run (default amcl)
    nav2_delay:=<seconds>     wait before starting Nav2 (default 12; raise it on
                              slow machines if controller_server fails to configure)
    headless:=true            Gazebo server only — no GUI, no GPU needed
    rviz:=false               skip RViz2 (no display available, or CI)
"""
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, DeclareLaunchArgument
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    pkg_bringup = get_package_share_directory('acadbot_bringup')
    pkg_courier = get_package_share_directory('acadbot_courier')

    autonomy = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_bringup, 'launch', 'autonomy.launch.py')),
        launch_arguments={
            'localization': LaunchConfiguration('localization'),
            'nav2_delay': LaunchConfiguration('nav2_delay'),
            'headless': LaunchConfiguration('headless'),
            'rviz': LaunchConfiguration('rviz'),
        }.items())

    courier = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_courier, 'launch', 'courier.launch.py')))

    return LaunchDescription([
        DeclareLaunchArgument(
            'localization', default_value='amcl',
            description="'amcl' (default) or 'slam' — passed through to autonomy.launch.py"),
        DeclareLaunchArgument(
            'nav2_delay', default_value='12.0',
            description='Seconds to wait for localization before starting Nav2.'),
        DeclareLaunchArgument(
            'headless', default_value='false',
            description='Run Gazebo server-only (no GUI, no GPU required).'),
        DeclareLaunchArgument(
            'rviz', default_value='true',
            description='Start RViz2. Set false on a machine with no display.'),
        autonomy,
        courier,
    ])
