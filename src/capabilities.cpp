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

std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::array<unsigned char, 16> uuid_bytes;
    for (int i = 0; i < 16; ++i) {
        uuid_bytes[i] = dis(gen);
    }
    uuid_bytes[6] = (uuid_bytes[6] & 0x0f) | 0x40;
    uuid_bytes[8] = (uuid_bytes[8] & 0x3f) | 0x80;
    std::stringstream ss;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            ss << "-";
        }
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)uuid_bytes[i];
    }
    return ss.str();
}

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
    BeaconNode() : Node("beacon_node") {
        // Namespace
        namespace_ = this->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }
        // Create a publisher and a subscriber on the same topic "beacons"
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Beacon>("beacon", 10);
        timer_ = this->create_wall_timer(10s, std::bind(&BeaconNode::timer_callback, this));

        // capability parameter
        my_beacon_function_ = this->declare_parameter("function", "harvester");
        my_beacon_uuid_ = this->declare_parameter("uuid", generate_uuid());
        my_beacon_color_ = this->declare_parameter("color", "#ff0000");

        // Initialize this node's own beacon.
        my_beacon_.uuid = my_beacon_uuid_;
        my_beacon_.function = my_beacon_function_;
        my_beacon_.name = namespace_;
        my_beacon_.color = my_beacon_color_;

        RCLCPP_INFO(this->get_logger(), "BeaconNode started with UUID: %s", my_beacon_.uuid.c_str());
    }

  private:
    void timer_callback() { publisher_->publish(my_beacon_); }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<BeaconNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
