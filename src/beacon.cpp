#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

namespace echo = spdlog;

class Beacon {
    private:
        rclcpp::Node::SharedPtr node;
   public:
        Beacon(rclcpp::Node::SharedPtr node) : node(node) {
            echo::info("Beacon created");
        }

        ~Beacon() {
            rclcpp::shutdown();
        }
};



int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);

    rclcpp::NodeOptions options_0;
    options_0.allow_undeclared_parameters(true);
    options_0.automatically_declare_parameters_from_overrides(true);
    auto node_0 = rclcpp::Node::make_shared("beacon", options_0);
    auto parser = Beacon(node_0);
    executor.add_node(node_0);

    executor.spin();
    rclcpp::shutdown();
    return 0;
}
