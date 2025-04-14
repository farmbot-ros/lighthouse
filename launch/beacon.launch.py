import os
import yaml
import re
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
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
    zero_ref_str = LaunchConfiguration("zero_ref").perform(context)
    color = LaunchConfiguration("color").perform(context)
    uuid = LaunchConfiguration("uuid").perform(context)
    offline = LaunchConfiguration("offline").perform(context)
    priority = LaunchConfiguration("priority").perform(context)
    chain_domain = LaunchConfiguration("chain_domain").perform(context)
    password = LaunchConfiguration("password").perform(context)
    key_file = LaunchConfiguration("key_file").perform(context)
    blockchain_arg = LaunchConfiguration("blockchain").perform(context)

    blockchain = blockchain_arg == "true"
    chain_domain_int = int(chain_domain)

    # Convert the zero_ref string to an actual list.
    try:
        zero_ref = yaml.safe_load(zero_ref_str)
    except Exception as e:
        raise RuntimeError(f"Failed to parse zero_ref: {e}")

    nodes_array = []

    # Example: Passing zero_ref into the 'capabilities' node parameters.
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
            {"private_key_file": key_file} if key_file != "" else {},
            # Pass the zero_ref parameter as a list of floats.
            {"zero_ref": zero_ref} if zero_ref else {},
        ],
        output="screen",
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
        output="screen",
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
            {"password": password} if password != "" else {},
            {"private_key_file": key_file} if key_file != "" else {},
        ],
        output="screen",
    )
    if blockchain:
        nodes_array.append(blocky)

    return nodes_array


def generate_launch_description():
    namespace_arg = DeclareLaunchArgument("namespace", default_value="fbot")
    function_arg = DeclareLaunchArgument("function", default_value="harvester")
    color_arg = DeclareLaunchArgument("color", default_value="#ff0000")
    zero_ref_arg = DeclareLaunchArgument(
        "zero_ref", default_value="[51.937587, 5.705458, 53.801823]"
    )
    uuid_arg = DeclareLaunchArgument("uuid", default_value=str(uuid4()))
    offline_arg = DeclareLaunchArgument("offline", default_value="60s")
    priority_arg = DeclareLaunchArgument("priority", default_value="100")
    chain_domain_arg = DeclareLaunchArgument("chain_domain", default_value="1")
    password_arg = DeclareLaunchArgument("password", default_value="")
    key_file_arg = DeclareLaunchArgument("key_file", default_value="")
    blockchain_arg = DeclareLaunchArgument("blockchain", default_value="true")

    return LaunchDescription(
        [
            namespace_arg,
            function_arg,
            zero_ref_arg,
            color_arg,
            uuid_arg,
            offline_arg,
            priority_arg,
            chain_domain_arg,
            password_arg,
            key_file_arg,
            blockchain_arg,
            OpaqueFunction(function=launch_setup),
        ]
    )
