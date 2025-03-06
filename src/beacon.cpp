#include "farmbot_interfaces/msg/beacons.hpp"
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
    bool got_self_beacon_ = false;
    // Internal storage for the received beacons array.
    std::vector<farmbot_interfaces::msg::Beacon> stored_beacons_;
    farmbot_interfaces::msg::Beacon my_beacon_;

    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Beacons>::SharedPtr publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr single_beacon_sub;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr all_beacons_sub;

    // timer
    rclcpp::TimerBase::SharedPtr timer_;

    // This node's own beacon.

  public:
    BeaconNode() : Node("beacon_node") {
        // Namespace
        namespace_ = this->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }
        // Create a publisher and a subscriber on the same topic "beacons"
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Beacons>("/beacons", 10);
        all_beacons_sub = this->create_subscription<farmbot_interfaces::msg::Beacons>(
            "/beacons", 10, std::bind(&BeaconNode::all_beacons_callback, this, _1));
        single_beacon_sub = this->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon", 10, std::bind(&BeaconNode::single_beacon_callback, this, _1));
        timer_ = this->create_wall_timer(10s, std::bind(&BeaconNode::timer_callback, this));

        RCLCPP_INFO(this->get_logger(), "BeaconNode started");
    }

  private:
    void timer_callback() {
        auto msg = std::make_shared<farmbot_interfaces::msg::Beacons>();
        // sort the beacons by uuid
        std::sort(stored_beacons_.begin(), stored_beacons_.end(),
                  [](const farmbot_interfaces::msg::Beacon &a, const farmbot_interfaces::msg::Beacon &b) {
                      return a.uuid < b.uuid;
                  });
        // remove duplicates based on uuid
        stored_beacons_.erase(std::unique(stored_beacons_.begin(), stored_beacons_.end(),
                                          [](const farmbot_interfaces::msg::Beacon &a,
                                             const farmbot_interfaces::msg::Beacon &b) { return a.uuid == b.uuid; }),
                              stored_beacons_.end());
        msg->beacons = stored_beacons_;
        publisher_->publish(*msg);
    }

    void single_beacon_callback(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        if (!got_self_beacon_) {
            my_beacon_ = *msg;
            stored_beacons_.push_back(my_beacon_);
            got_self_beacon_ = true;
        }
    }

    void all_beacons_callback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        // Check for every beacon if it is in the stored array.
        for (auto m_beacon : msg->beacons) {
            for (auto s_beacon : stored_beacons_) {
                if (s_beacon.uuid == m_beacon.uuid) {
                    return;
                }
            }
            stored_beacons_.push_back(m_beacon);
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
