#include <chrono>
#include <farmbot_interfaces/msg/detail/beacons__struct.hpp>
#include <iostream>
#include <sstream>
#include <string>

#include "blocky/chain.hpp"

#include <farmbot_interfaces/msg/beacon.hpp>
#include <farmbot_interfaces/msg/beacons.hpp>
#include <farmbot_interfaces/msg/chain.hpp>
#include <farmbot_interfaces/srv/join_chain.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

using namespace std::chrono;
using namespace std::placeholders;
using namespace std::chrono_literals;

class BlockyNode {
  private:
    rclcpp::Node::SharedPtr node_;
    std::string namespace_;
    int32_t chain_domain_;

    farmbot_interfaces::msg::Beacon beacon_;
    farmbot_interfaces::msg::Beacons beacons_;

    chain::Chain chain_;
    std::shared_ptr<chain::Crypto> crypto_;
    std::string private_key_file_;
    bool chain_initialized_;
    bool got_beacons_;
    bool in_chain_;

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr public_key_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr beacon_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr beacons_sub_;

    rclcpp::TimerBase::SharedPtr chain_publish_;

    using jc = farmbot_interfaces::srv::JoinChain;
    rclcpp::Service<jc>::SharedPtr join_service_;
    rclcpp::Client<jc>::SharedPtr join_client_;
    rclcpp::CallbackGroup::SharedPtr client_group_, service_group_;
    rmw_qos_profile_t qos_profile;
    // rclcpp::Service<farmbot_interfaces::srv::LeaveChain>::SharedPtr leave_service_;

  public:
    BlockyNode(rclcpp::Node::SharedPtr node) : node_(node) {
        RCLCPP_INFO(node_->get_logger(), "BlockyNode started");
        // Namespace
        namespace_ = node_->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_ = namespace_.substr(1);
        }

        private_key_file_ = node_->get_parameter_or<std::string>("private_key_file", "private_key.pem");
        chain_domain_ = node_->get_parameter_or<int32_t>("chain_domain", 987);

        crypto_ = std::make_shared<chain::Crypto>(private_key_file_);

        client_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        service_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        qos_profile = rmw_qos_profile_services_default;
    }

    void setup() {
        chain_pub_ = node_->create_publisher<farmbot_interfaces::msg::Chain>("/chain", 10);
        public_key_pub_ = node_->create_publisher<std_msgs::msg::String>("public_key", 10);

        beacon_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon/rci", 10, std::bind(&BlockyNode::genesisCreate, this, _1));
        beacons_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacons>(
            "/beacons/rci", 10, std::bind(&BlockyNode::beacons_callback, this, _1));
        chain_sub_ = node_->create_subscription<farmbot_interfaces::msg::Chain>(
            "/chain", 10, std::bind(&BlockyNode::chainCallback, this, _1));

        chain_publish_ = node_->create_wall_timer(1s, std::bind(&BlockyNode::chainPubT, this));

        join_service_ =
            node_->create_service<jc>("join_chain", std::bind(&BlockyNode::join_service_callback, this, _1, _2));
        // join_client_ = node_->create_client<jc>("join_chain", qos_profile, client_group_);

        RCLCPP_INFO(node_->get_logger(), "BeaconNode started");
    }

    void genesisCreate(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        if (!chain_initialized_) {
            // genesis block
            RCLCPP_INFO(node_->get_logger(), " *** [%s] Chain created and genesis block added", namespace_.c_str());
            chain_ = chain::Chain(std::to_string(chain_domain_), msg->priority, msg->uuid, msg->function, crypto_);
            chain_initialized_ = true, in_chain_ = true;
        }
        beacon_ = *msg;
        beacon_sub_.reset();
    }

    void chainCallback(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        if (msg->uuid != std::to_string(chain_domain_)) {
            return;
        }
        if (!chain_initialized_) {
            RCLCPP_INFO(node_->get_logger(), " *** [%s] Chain adopted from an existing /chain", namespace_.c_str());
            chain_ = chain::Chain(*msg);
            chain_initialized_ = true;
        }
        if (!in_chain_ && got_beacons_) {
            std::string whom_to_ask = getNameFromUUID(msg->chain[0].transactions[0].uuid);
            RCLCPP_INFO(node_->get_logger(), " *** Asking >>> %s <<< to join the chain", whom_to_ask.c_str());
            join_client_ = node_->create_client<jc>("/" + whom_to_ask + "/join_chain", qos_profile, client_group_);
            request_join();
            chain_sub_.reset();
        }
    }

    void chainPubT() {
        if (in_chain_) {
            chain_pub_->publish(chain_.toMsg());
        }
        std_msgs::msg::String public_key_msg;
        public_key_msg.data = crypto_->getPublicHalf();
        public_key_pub_->publish(public_key_msg);

        // chain::Transaction join_transaction(10, "000", "harvester");
        // join_transaction.signTransaction(crypto_);
        // chain_.addBlock(chain::Block("0", {join_transaction}));
    }

    void beacons_callback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        beacons_ = *msg;
        got_beacons_ = true;
    }

    void join_service_callback(const std::shared_ptr<jc::Request> req, std::shared_ptr<jc::Response> res) {
        RCLCPP_INFO(node_->get_logger(), " -- Robot %s wants to join the chain", req->robot_uuid.c_str());
        RCLCPP_INFO(node_->get_logger(), " -- Chain length is at: %zu", chain_.chain_.size());
        res->success = true;
        chain::Transaction join_transaction(10, req->robot_uuid, "harvester");
        join_transaction.signTransaction(crypto_);
        chain_.addBlock(chain::Block({join_transaction}));
        RCLCPP_INFO(node_->get_logger(), " -- Joined the chain");
        RCLCPP_INFO(node_->get_logger(), " -- Chain length became: %zu", chain_.chain_.size());
        res->chain = chain_.toMsg();
    }

    void request_join() {
        RCLCPP_INFO(node_->get_logger(), " -- Requesting to join the chain");
        auto request = std::make_shared<jc::Request>();
        request->robot_uuid = namespace_;
        request->encrypted_password = crypto_->getPublicHalf();
        while (!join_client_->wait_for_service(1s)) {
            if (!rclcpp::ok()) {
                RCLCPP_ERROR(node_->get_logger(), " -- Join service not available, node shutting down");
                return;
            }
            RCLCPP_INFO(node_->get_logger(), " -- Waiting for service to appear...");
        }
        auto result_future = join_client_->async_send_request(request);
    }

  private:
    std::string getNameFromUUID(const std::string &uuid) {
        std::string name;
        for (const auto &beacon : beacons_.beacons) {
            if (beacon.uuid == uuid) {
                name = beacon.name;
                break;
            }
        }
        return name;
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
    blocky->setup();

    executor.add_node(blocky_node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}
