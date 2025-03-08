#include "farmbot_interfaces/msg/beacons.hpp"
#include "rclcpp/rclcpp.hpp"

#include <rclcpp/parameter_value.hpp>
#include <rclcpp/timer.hpp>
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
    int timer_to_offline;
    // Internal storage for the received beacons array.
    std::vector<farmbot_interfaces::msg::Beacon> stored_beacons_;
    farmbot_interfaces::msg::Beacon my_beacon_;

    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Beacons>::SharedPtr publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr single_beacon_sub;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr all_beacons_sub;

    // timer
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::TimerBase::SharedPtr offline_timer_;

    // This node's own beacon.

  public:
    BeaconNode() : Node("beacon_node") {
        // Namespace
        namespace_ = this->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        this->declare_parameter("offline", rclcpp::PARAMETER_INTEGER);
        timer_to_offline = this->get_parameter_or<int>("offline", 60);

        // "beacons/rci" (rci stands for "Robot Capabilitiy Index")
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Beacons>("/beacons/rci", 10);
        all_beacons_sub = this->create_subscription<farmbot_interfaces::msg::Beacons>(
            "/beacons/rci", 10, std::bind(&BeaconNode::all_beacons_callback, this, _1));
        single_beacon_sub = this->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon/rci", 10, std::bind(&BeaconNode::single_beacon_callback, this, _1));
        timer_ = this->create_wall_timer(10s, std::bind(&BeaconNode::timer_callback, this));
        offline_timer_ = this->create_wall_timer(1s, std::bind(&BeaconNode::offline_timer_callback, this));

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
        msg->sender = namespace_;
        msg->num_beacons = stored_beacons_.size();
        publisher_->publish(*msg);
    }

    void offline_timer_callback() {
        auto time_now = this->now();
        stored_beacons_.erase(std::remove_if(stored_beacons_.begin(), stored_beacons_.end(),
                                             [&](const farmbot_interfaces::msg::Beacon &beacon) {
                                                 return (time_now - beacon.timestamp) >
                                                        rclcpp::Duration::from_seconds(timer_to_offline);
                                             }),
                              stored_beacons_.end());
    }

    void single_beacon_callback(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        // delete old message and add new message
        stored_beacons_.erase(
            std::remove_if(stored_beacons_.begin(), stored_beacons_.end(),
                           [&](const farmbot_interfaces::msg::Beacon &beacon) { return (msg->uuid == beacon.uuid); }),
            stored_beacons_.end());
        stored_beacons_.push_back(*msg);
    }

    void all_beacons_callback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        // RCLCPP_INFO(this->get_logger(), "Number of beacons %zu", msg->beacons.size());
        for (const auto &m_beacon : msg->beacons) {
            bool found = false;
            for (const auto &s_beacon : stored_beacons_) {
                if (s_beacon.uuid == m_beacon.uuid) {
                    found = true;
                    break; // Beacon already exists, skip adding it.
                } else if ((this->now() - m_beacon.timestamp) > rclcpp::Duration::from_seconds(timer_to_offline)) {
                    found = true;
                    break; // Beacon is too old to add, it's just crawling around.
                }
            }
            if (!found) {
                stored_beacons_.push_back(m_beacon);
            }
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
