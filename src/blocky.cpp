#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "blocky/chain.hpp"
#include <rclcpp/rclcpp.hpp>

using namespace std::chrono;
using namespace std::placeholders;
using namespace std::chrono_literals;

class BlockyNode {
  private:
    rclcpp::Node::SharedPtr node_;
    std::string namespace_;

  public:
    BlockyNode(rclcpp::Node::SharedPtr node) : node_(node) { RCLCPP_INFO(node_->get_logger(), "BlockyNode started"); }
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
