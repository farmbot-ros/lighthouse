#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "blocky/chain.hpp"
#include "blocky/signer.cpp"

#include <farmbot_interfaces/msg/beacon.hpp>
#include <farmbot_interfaces/msg/chain.hpp>
#include <rclcpp/rclcpp.hpp>

using namespace std::chrono;
using namespace std::placeholders;
using namespace std::chrono_literals;

class BlockyNode {
  private:
    rclcpp::Node::SharedPtr node_;
    std::string namespace_;

    chain::Chain chain_;
    bool genesis_initialized_;

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr beacon_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;

    rclcpp::TimerBase::SharedPtr chain_publish_;

  public:
    BlockyNode(rclcpp::Node::SharedPtr node) : node_(node) {
        RCLCPP_INFO(node_->get_logger(), "BlockyNode started");
        // Namespace
        namespace_ = node_->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        chain_pub_ = node_->create_publisher<farmbot_interfaces::msg::Chain>("/chain", 10);
        beacon_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon/rci", 10, std::bind(&BlockyNode::beacon_callback, this, _1));

        RCLCPP_INFO(node_->get_logger(), "BeaconNode started");
    }

    void beacon_callback(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        if (!genesis_initialized_) {
            chain_ = chain::Chain(msg->priority, msg->uuid, msg->function);
            genesis_initialized_ = true;
        }
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
    rclcpp::NodeOptions options;
    options.allow_undeclared_parameters(true);
    options.automatically_declare_parameters_from_overrides(true);

    rclcpp::Node::SharedPtr blocky_node = rclcpp::Node::make_shared("blocky_node", options);
    std::shared_ptr<BlockyNode> blocky = std::make_shared<BlockyNode>(blocky_node);

    executor.add_node(blocky_node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}
