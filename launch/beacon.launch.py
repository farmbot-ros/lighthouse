import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
import yaml
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
from launch.substitutions import LaunchConfiguration
from launch.actions import OpaqueFunction
from uuid import uuid4


def launch_setup(context, *args, **kwargs):
    namespace = LaunchConfiguration("namespace").perform(context)
    function = LaunchConfiguration("function").perform(context)
    color = LaunchConfiguration("color").perform(context)
    uuid = LaunchConfiguration("uuid").perform(context)
    param_file = os.path.join(
        get_package_share_directory("farmbot_lighthouse"), "config", "params.yaml"
    )

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
        ],
    )
    nodes_array.append(beacon)

    return nodes_array


def generate_launch_description():
    namespace_arg = DeclareLaunchArgument("namespace", default_value="fbot")
    function = DeclareLaunchArgument("function", default_value="harvester")
    color = DeclareLaunchArgument("color", default_value="#ff0000")
    uuid = DeclareLaunchArgument("uuid", default_value=str(uuid4()))

    return LaunchDescription(
        [
            namespace_arg,
            function,
            color,
            uuid,
            OpaqueFunction(function=launch_setup),
        ]
    )
