#include <chrono>
#include <farmbot_interfaces/msg/detail/beacons__struct.hpp>
#include <iostream>
#include <rclcpp/executors.hpp>
#include <rclcpp/subscription_options.hpp>
#include <sstream>
#include <std_msgs/msg/detail/string__struct.hpp>
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
    bool chain_initialized_, in_chain_, got_beacons_, got_beacon_, got_target_key_;

    rclcpp::CallbackGroup::SharedPtr client_group_, keysub_geoup_;

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr public_key_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr beacon_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr beacons_sub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr target_key_sub_;
    rclcpp::SubscriptionOptions sub_options;

    rclcpp::TimerBase::SharedPtr chain_publish_;

    using JoinChain = farmbot_interfaces::srv::JoinChain;
    rclcpp::Service<JoinChain>::SharedPtr join_service_;
    rclcpp::Client<JoinChain>::SharedPtr target_permission_client_;
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
        RCLCPP_INFO(node_->get_logger(), " *** Private key file: %s", private_key_file_.c_str());
        chain_domain_ = node_->get_parameter_or<int32_t>("chain_domain", 987);

        crypto_ = std::make_shared<chain::Crypto>(private_key_file_);

        client_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        keysub_geoup_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        sub_options.callback_group = keysub_geoup_;
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
            node_->create_service<JoinChain>("join_chain", std::bind(&BlockyNode::join_response, this, _1, _2));

        RCLCPP_INFO(node_->get_logger(), "BeaconNode started");
    }

    void genesisCreate(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        if (!chain_initialized_) {
            // genesis block
            RCLCPP_INFO(node_->get_logger(), " *** [%s] Chain created and genesis block added", namespace_.c_str());
            chain_ = chain::Chain(std::to_string(chain_domain_), msg->uuid, msg->function, crypto_);
            chain_initialized_ = true, in_chain_ = true;
        }
        beacon_ = *msg;
        got_beacon_ = true;
        beacon_sub_.reset();
    }
    void chainPubT() {
        if (in_chain_) {
            chain_pub_->publish(chain_.toMsg());
        }
        std_msgs::msg::String public_key_msg;
        public_key_msg.data = crypto_->getPublicHalf();
        public_key_pub_->publish(public_key_msg);
    }

    void beacons_callback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        beacons_ = *msg;
        got_beacons_ = true;
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
        if (!in_chain_ && got_beacons_ && got_beacon_) {
            RCLCPP_INFO(node_->get_logger(), " R_UUID: %s", beacon_.uuid.c_str());
            for (const auto &block : chain_.blocks_) {
                for (const auto &transaction : block.transactions_) {
                    RCLCPP_INFO(node_->get_logger(), " T_UUID: %s", transaction.uuid_.c_str());
                    if (transaction.uuid_ == beacon_.uuid) {
                        in_chain_ = true, chain_initialized_ = true;
                        RCLCPP_INFO(node_->get_logger(), " *** [%s] I exist in the chain, thus not joining",
                                    namespace_.c_str());
                        return;
                    }
                }
            }
            in_chain_ = true;
            join_request(msg);
        }
    }

    void join_response(const std::shared_ptr<JoinChain::Request> req, std::shared_ptr<JoinChain::Response> res) {
        RCLCPP_INFO(node_->get_logger(), " -- Robot %s wants to join the chain", req->robot_uuid.c_str());
        std::string password_enc = req->encrypted_password;
        // TODO: check if the password is valid (something is going wrong here)
        auto password = crypto_->decrypt(chain::stringToVector(password_enc));
        // RCLCPP_INFO(node_->get_logger(), " -- Password: %s", chain::vectorToString(password).c_str());

        chain_.addBlock(req->robot_uuid, "harvester", crypto_);
        RCLCPP_INFO(node_->get_logger(), " -- Joined the chain");
        res->chain = chain_.toMsg();
        res->success = true;
    }

    void join_request(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        std::string whom_to_ask = getNameFromUUID(msg->chain[0].transactions[0].uuid);
        RCLCPP_INFO(node_->get_logger(), " *** Asking >>> %s <<< to join the chain", whom_to_ask.c_str());
        target_permission_client_ =
            node_->create_client<JoinChain>("/" + whom_to_ask + "/join_chain", qos_profile, client_group_);
        std::string target_key;
        target_key_sub_ = node_->create_subscription<std_msgs::msg::String>(
            "/" + whom_to_ask + "/public_key", 10,
            [&](const std_msgs::msg::String::SharedPtr msg) {
                target_key = msg->data;
                got_target_key_ = true;
                target_key_sub_.reset();
            },
            sub_options);
        RCLCPP_INFO(node_->get_logger(), " *** Waiting for target key");
        while (!got_target_key_ && rclcpp::ok()) {
            rclcpp::sleep_for(100ms);
        }
        RCLCPP_INFO(node_->get_logger(), " *** Got target public key \n\n%s", target_key.c_str());

        auto request = std::make_shared<JoinChain::Request>();
        request->robot_uuid = beacon_.uuid;
        auto password = "password";
        auto public_key = chain::loadPublicKeyFromPEM(target_key);
        chain::encrypt(public_key, password);
        request->encrypted_password = password;
        while (!target_permission_client_->wait_for_service(1s)) {
            if (!rclcpp::ok()) {
                RCLCPP_ERROR(node_->get_logger(), " -- Join service not available, node shutting down");
                return;
            }
            RCLCPP_INFO(node_->get_logger(), " -- Waiting for service to appear...");
        }
        auto result_future = target_permission_client_->async_send_request(request);
        while (rclcpp::ok() && result_future.wait_for(1s) == std::future_status::timeout) {
            RCLCPP_INFO(node_->get_logger(), "Waiting for response from GPS2ENU service...");
        }
        auto result = result_future.get();
        if (result->success) {
            chain_ = chain::Chain(result->chain);

            RCLCPP_INFO(node_->get_logger(), " -- Joined the chain");
        }
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
