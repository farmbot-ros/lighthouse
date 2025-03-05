#include "farmbot_interfaces/msg/beacon.hpp"
#include "farmbot_interfaces/msg/beacons.hpp"
#include "rclcpp/rclcpp.hpp"

#include <string>
#include <vector>

#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
// Cross-platform UUID generation (requires C++11 or later)
std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::array<unsigned char, 16> uuid_bytes;
    for (int i = 0; i < 16; ++i) {
        uuid_bytes[i] = dis(gen);
    }
    // Set the UUID version (version 4 - random)
    uuid_bytes[6] = (uuid_bytes[6] & 0x0f) | 0x40;
    // Set the UUID variant (variant 1 - RFC 4122)
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
    // Internal storage for the received beacons array.
    std::vector<farmbot_interfaces::msg::Beacon> stored_beacons_;

    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Beacons>::SharedPtr publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr subscription_;

    // This node's own beacon.
    farmbot_interfaces::msg::Beacon my_beacon_;
    std::string my_beacon_uuid_;
    std::string my_beacon_capability_;

  public:
    BeaconNode() : Node("beacon_node") {
        // Create a publisher and a subscriber on the same topic "beacons"
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Beacons>("beacons", 10);
        subscription_ = this->create_subscription<farmbot_interfaces::msg::Beacons>(
            "beacons", 10, std::bind(&BeaconNode::beaconCallback, this, std::placeholders::_1));

        // capability parameter
        my_beacon_capability_ = this->declare_parameter("capability", "harvester");
        my_beacon_uuid_ = this->declare_parameter("uuid", generate_uuid());

        // Initialize this node's own beacon.
        my_beacon_.uuid = my_beacon_uuid_;
        my_beacon_.capability = my_beacon_capability_;

        RCLCPP_INFO(this->get_logger(), "BeaconNode started with UUID: %s", my_beacon_.uuid.c_str());
    }

  private:
    void beaconCallback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        // Store the incoming array internally.
        stored_beacons_ = msg->beacons;

        // Check if our beacon is already present.
        bool found = false;
        for (const auto &beacon : msg->beacons) {
            if (beacon.uuid == my_beacon_.uuid) {
                found = true;
                break;
            }
        }

        // If our beacon is not found, append it and publish the updated array.
        if (!found) {
            auto updated_msg = *msg; // Start with the received beacons array.
            updated_msg.beacons.push_back(my_beacon_);
            RCLCPP_INFO(this->get_logger(), "Beacon not found. Publishing updated beacons message.");
            publisher_->publish(updated_msg);
        } else {
            RCLCPP_INFO(this->get_logger(), "My beacon already exists in the message. Ignoring update.");
        }
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<BeaconNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
