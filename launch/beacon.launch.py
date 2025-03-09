import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
import yaml
import re
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
from launch.substitutions import LaunchConfiguration
from launch.actions import OpaqueFunction
from uuid import uuid4

param_file = os.path.join(
    get_package_share_directory("farmbot_lighthouse"), "config", "params.yaml"
)


def convert_to_seconds(time_str):
    match = re.match(r"(\d+)([smh])", time_str)
    if not match:
        raise ValueError("Invalid time format. Use Xs, Xm, or Xh.")
    value, unit = int(match.group(1)), match.group(2)
    conversion = {"s": 1, "m": 60, "h": 3600}
    return value * conversion[unit]


def launch_setup(context, *args, **kwargs):
    namespace = LaunchConfiguration("namespace").perform(context)
    function = LaunchConfiguration("function").perform(context)
    color = LaunchConfiguration("color").perform(context)
    uuid = LaunchConfiguration("uuid").perform(context)
    offline = LaunchConfiguration("offline").perform(context)
    priority = LaunchConfiguration("priority").perform(context)
    chain_domain = LaunchConfiguration("chain_domain").perform(context)

    # convert offile string to seconds, the string can be in the format of "10s" or "10m" or "10h"

    chain_domain_int = int(chain_domain)

    nodes_array = []

    capabilities = Node(
        package="farmbot_lighthouse",
        executable="capabilities",
        name="capabilities",
        namespace=namespace,
        parameters=[
            yaml.safe_load(open(param_file))["capabilities"]["ros__parameters"],
            yaml.safe_load(open(param_file))["global"]["ros__parameters"],
            {"function": function} if function != "" else {},
            {"color": color} if color != "" else {},
            {"uuid": uuid} if uuid != "" else {},
            {"priority": priority} if priority != "" else {},
        ],
    )
    nodes_array.append(capabilities)

    beacon = Node(
        package="farmbot_lighthouse",
        executable="beacon",
        name="beacon",
        namespace=namespace,
        parameters=[
            yaml.safe_load(open(param_file))["beacon"]["ros__parameters"],
            yaml.safe_load(open(param_file))["global"]["ros__parameters"],
            {"offline": convert_to_seconds(offline)},
        ],
    )
    nodes_array.append(beacon)

    blocky = Node(
        package="farmbot_lighthouse",
        executable="blocky",
        name="blocky",
        namespace=namespace,
        parameters=[
            yaml.safe_load(open(param_file))["blocky"]["ros__parameters"],
            yaml.safe_load(open(param_file))["global"]["ros__parameters"],
            {"chain_domain": chain_domain_int},
        ],
        output="screen",
    )
    nodes_array.append(blocky)

    return nodes_array


def generate_launch_description():
    namespace_arg = DeclareLaunchArgument("namespace", default_value="fbot")
    function = DeclareLaunchArgument("function", default_value="harvester")
    color = DeclareLaunchArgument("color", default_value="#ff0000")
    uuid = DeclareLaunchArgument("uuid", default_value=str(uuid4()))
    offline = DeclareLaunchArgument("offline", default_value="60s")
    priority = DeclareLaunchArgument("priority", default_value="0")
    chain_domain = DeclareLaunchArgument("chain_domain", default_value="1")

    return LaunchDescription(
        [
            namespace_arg,
            function,
            color,
            uuid,
            offline,
            priority,
            chain_domain,
            OpaqueFunction(function=launch_setup),
        ]
    )
