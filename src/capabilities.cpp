#include "blocky/signer.hpp"
#include "farmbot_interfaces/msg/agent.hpp"
#include "farmbot_interfaces/msg/participant.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "rclcpp/rclcpp.hpp"

#include <string>

#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

using namespace std::chrono_literals;
using namespace std::placeholders;

class CapClass {
  private:
    std::string namespace_;
    rclcpp::Node::SharedPtr node_;
    farmbot_interfaces::msg::Agent my_beacon_;
    std::shared_ptr<chain::Crypto> crypto_;

    // Publisher and Subscriber.
    rclcpp::Publisher<farmbot_interfaces::msg::Agent>::SharedPtr publisher_;
    // timer
    rclcpp::TimerBase::SharedPtr timer_;
    // This node's own beacon.
    std::string my_beacon_uuid_;
    std::string my_beacon_function_;
    std::string my_beacon_color_;
    std::string my_beacon_pub_key_;
    std::vector<double> my_beacon_zero_ref_;

  public:
    CapClass(rclcpp::Node::SharedPtr node) : node_(node) {
        // Namespace
        namespace_ = node_->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        // Create a publisher and a subscriber on the same topic beacons/rci" (rci stands for "Robot Capabilitiy Index")
        publisher_ = node_->create_publisher<farmbot_interfaces::msg::Agent>("beacon/rci", 10);
        timer_ = node_->create_wall_timer(3s, std::bind(&CapClass::timer_callback, this));

        // capability parameter
        my_beacon_function_ = node_->get_parameter_or<std::string>("function", "harvester");
        my_beacon_uuid_ = node_->get_parameter_or<std::string>("uuid", "00000000-0000-0000-0000-000000000000");
        my_beacon_color_ = node_->get_parameter_or<std::string>("color", "#ff0000");
        my_beacon_zero_ref_ = node_->get_parameter_or<std::vector<double>>("zero_ref", {0.0, 0.0, 0.0});

        auto private_key_file_ = node_->get_parameter_or<std::string>("private_key_file", "private_key.pem");
        //
        RCLCPP_INFO(node_->get_logger(), "Private key file: %s", private_key_file_.c_str());
        crypto_ = std::make_shared<chain::Crypto>(private_key_file_);
        my_beacon_pub_key_ = crypto_->getPublicHalf();
        RCLCPP_INFO(node_->get_logger(), "Robots %s private key: \n\n%s", namespace_.c_str(),
                    my_beacon_pub_key_.c_str());

        farmbot_interfaces::msg::Participant participant;

        participant.uuid = my_beacon_uuid_;
        participant.functions.push_back(my_beacon_function_);
        participant.color = my_beacon_color_;

        geometry_msgs::msg::Point zero_ref;
        zero_ref.x = my_beacon_zero_ref_[0];
        zero_ref.y = my_beacon_zero_ref_[1];
        zero_ref.z = my_beacon_zero_ref_[2];
        my_beacon_.zero_ref = zero_ref;

        // Initialize this node's own beacon.
        my_beacon_.uuid = my_beacon_uuid_;
        my_beacon_.name = namespace_;
        my_beacon_.participants.push_back(participant);
        my_beacon_.public_key = my_beacon_pub_key_;

        RCLCPP_INFO(node_->get_logger(), "ROBOT with UUID: %s", my_beacon_.uuid.c_str());
    }

  private:
    void timer_callback() {
        builtin_interfaces::msg::Time *timestamp = new builtin_interfaces::msg::Time();
        timestamp->sec = node_->now().seconds();
        timestamp->nanosec = node_->now().nanoseconds();
        my_beacon_.timestamp = *timestamp;
        publisher_->publish(my_beacon_);
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    rclcpp::NodeOptions options;
    options.allow_undeclared_parameters(true);
    options.automatically_declare_parameters_from_overrides(true);

    rclcpp::Node::SharedPtr cap_node = rclcpp::Node::make_shared("cap_node", options);
    std::shared_ptr<CapClass> blocky = std::make_shared<CapClass>(cap_node);
    // blocky->setup();

    executor.add_node(cap_node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}

// int main(int argc, char *argv[]) {
//     rclcpp::init(argc, argv);
//     auto node = std::make_shared<BeaconNode>();
//     rclcpp::spin(node);
//     rclcpp::shutdown();
//     return 0;
// }
