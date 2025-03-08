#include "farmbot_interfaces/msg/beacon.hpp"
#include "rclcpp/rclcpp.hpp"

#include <string>
#include <vector>

#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

using namespace std::chrono_literals;
using namespace std::placeholders;

class BeaconNode : public rclcpp::Node {
  private:
    std::string namespace_;
    farmbot_interfaces::msg::Beacon my_beacon_;
    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Beacon>::SharedPtr publisher_;
    // timer
    rclcpp::TimerBase::SharedPtr timer_;
    // This node's own beacon.
    std::string my_beacon_uuid_;
    std::string my_beacon_function_;
    std::string my_beacon_color_;

  public:
    BeaconNode()
        : Node("beacon_node",
               rclcpp::NodeOptions().allow_undeclared_parameters(true).automatically_declare_parameters_from_overrides(
                   true)) {
        // Namespace
        namespace_ = this->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        // Create a publisher and a subscriber on the same topic beacons/rci" (rci stands for "Robot Capabilitiy Index")
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Beacon>("beacon/rci", 10);
        timer_ = this->create_wall_timer(3s, std::bind(&BeaconNode::timer_callback, this));

        // capability parameter
        my_beacon_function_ = this->get_parameter_or<std::string>("function", "harvester");
        my_beacon_uuid_ = this->get_parameter_or<std::string>("uuid", "00000000-0000-0000-0000-000000000000");
        my_beacon_color_ = this->get_parameter_or<std::string>("color", "#ff0000");
        // my_beacon_uuid_ = this->declare_parameter("uuid", generate_uuid());
        // my_beacon_color_ = this->declare_parameter("color", "#ff0000");
        // Initialize this node's own beacon.
        my_beacon_.uuid = my_beacon_uuid_;
        my_beacon_.function = my_beacon_function_;
        my_beacon_.name = namespace_;
        my_beacon_.color = my_beacon_color_;

        RCLCPP_INFO(this->get_logger(), "ROBOT with UUID: %s", my_beacon_.uuid.c_str());
    }

  private:
    void timer_callback() {
        builtin_interfaces::msg::Time *timestamp = new builtin_interfaces::msg::Time();
        timestamp->sec = this->now().seconds();
        timestamp->nanosec = this->now().nanoseconds();
        my_beacon_.timestamp = *timestamp;
        publisher_->publish(my_beacon_);
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<BeaconNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
