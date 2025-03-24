#include "farmbot_interfaces/msg/agents.hpp"
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

class CapabilitiesNode : public rclcpp::Node {
  private:
    std::string namespace_;
    int timer_to_offline;
    // Internal storage for the received beacons array.
    std::vector<farmbot_interfaces::msg::Agent> stored_beacons_;
    farmbot_interfaces::msg::Agent my_beacon_;

    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Agents>::SharedPtr publisher_;
    rclcpp::Subscription<farmbot_interfaces::msg::Agent>::SharedPtr single_beacon_sub;
    rclcpp::Subscription<farmbot_interfaces::msg::Agents>::SharedPtr all_beacons_sub;

    // timer
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::TimerBase::SharedPtr offline_timer_;

    // This node's own beacon.

  public:
    CapabilitiesNode() : Node("beacon_node") {
        // Namespace
        namespace_ = this->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        this->declare_parameter("offline", rclcpp::PARAMETER_INTEGER);
        timer_to_offline = this->get_parameter_or<int>("offline", 0);

        // "beacons/rci" (rci stands for "Robot Capabilitiy Index")
        publisher_ = this->create_publisher<farmbot_interfaces::msg::Agents>("/beacons/rci", 10);
        all_beacons_sub = this->create_subscription<farmbot_interfaces::msg::Agents>(
            "/beacons/rci", 10, std::bind(&CapabilitiesNode::all_beacons_callback, this, _1));
        single_beacon_sub = this->create_subscription<farmbot_interfaces::msg::Agent>(
            "beacon/rci", 10, std::bind(&CapabilitiesNode::single_beacon_callback, this, _1));
        timer_ = this->create_wall_timer(10s, std::bind(&CapabilitiesNode::timer_callback, this));
        if (timer_to_offline > 0) {
            offline_timer_ = this->create_wall_timer(1s, std::bind(&CapabilitiesNode::offline_timer_callback, this));
        }

        RCLCPP_INFO(this->get_logger(), "BeaconNode started");
    }

  private:
    void timer_callback() {
        auto msg = std::make_shared<farmbot_interfaces::msg::Agents>();
        // sort the beacons by uuid
        std::sort(stored_beacons_.begin(), stored_beacons_.end(),
                  [](const farmbot_interfaces::msg::Agent &a, const farmbot_interfaces::msg::Agent &b) {
                      return a.uuid < b.uuid;
                  });
        // remove duplicates based on uuid
        stored_beacons_.erase(std::unique(stored_beacons_.begin(), stored_beacons_.end(),
                                          [](const farmbot_interfaces::msg::Agent &a,
                                             const farmbot_interfaces::msg::Agent &b) { return a.uuid == b.uuid; }),
                              stored_beacons_.end());
        msg->beacons = stored_beacons_;
        msg->sender = namespace_;
        msg->num_beacons = stored_beacons_.size();
        publisher_->publish(*msg);
    }

    void offline_timer_callback() {
        auto time_now = this->now();
        // stored_beacons_.erase(std::remove_if(stored_beacons_.begin(), stored_beacons_.end(),
        //                                      [&](const farmbot_interfaces::msg::Agent &beacon) {
        //                                          return (time_now - beacon.timestamp) >
        //                                                 rclcpp::Duration::from_seconds(timer_to_offline);
        //                                      }),
        //                       stored_beacons_.end());
    }

    void single_beacon_callback(const farmbot_interfaces::msg::Agent::SharedPtr msg) {
        // delete old message and add new message
        for (auto &beacon : stored_beacons_) {
            if (beacon.uuid == msg->uuid) {
                beacon = *msg;
                return;
            }
        }
        stored_beacons_.push_back(*msg);
    }

    void all_beacons_callback(const farmbot_interfaces::msg::Agents::SharedPtr msg) {
        // RCLCPP_INFO(this->get_logger(), "Number of beacons %zu", msg->beacons.size());
        for (const auto &m_beacon : msg->beacons) {
            for (auto &s_beacon : stored_beacons_) {
                if (s_beacon.uuid == m_beacon.uuid) {
                    if (m_beacon.timestamp.sec > s_beacon.timestamp.sec) {
                        s_beacon = m_beacon;
                    }
                }
                stored_beacons_.push_back(m_beacon);
            }
        }
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<CapabilitiesNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
