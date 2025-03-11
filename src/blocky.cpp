#include <chrono>
#include <farmbot_interfaces/msg/detail/beacons__struct.hpp>
#include <iostream>
#include <random>
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
#include <farmbot_interfaces/srv/vote_chain.hpp>
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
    std::string password;
    bool vote_policy_ = true;

    farmbot_interfaces::msg::Beacon beacon_;
    farmbot_interfaces::msg::Beacons beacons_;
    std::vector<std::pair<std::string, std::string>> uuids_;

    chain::Chain chain_;
    std::shared_ptr<chain::Crypto> crypto_;
    std::string private_key_file_;
    bool chain_initialized_, in_chain_, got_beacons_, got_beacon_, got_target_key_;

    rclcpp::CallbackGroup::SharedPtr client_group_, keysub_geoup_, service_group_;
    rmw_qos_profile_t qos_profile;

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;

    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr beacon_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacons>::SharedPtr beacons_sub_;
    rclcpp::SubscriptionOptions sub_options;

    rclcpp::TimerBase::SharedPtr chain_publish_;

    using JoinChain = farmbot_interfaces::srv::JoinChain;
    rclcpp::Service<JoinChain>::SharedPtr join_service_;
    rclcpp::Client<JoinChain>::SharedPtr target_permission_client_;

    using VoteChain = farmbot_interfaces::srv::VoteChain;
    rclcpp::Service<VoteChain>::SharedPtr vote_service_;
    rclcpp::Client<VoteChain>::SharedPtr target_vote_client_;

  public:
    BlockyNode(rclcpp::Node::SharedPtr node) : node_(node) {
        RCLCPP_INFO(node_->get_logger(), "BlockyNode started");
        // Namespace
        namespace_ = node_->get_namespace();

        private_key_file_ = node_->get_parameter_or<std::string>("private_key_file", "private_key.pem");
        RCLCPP_INFO(node_->get_logger(), " *** Private key file: %s", private_key_file_.c_str());
        chain_domain_ = node_->get_parameter_or<int32_t>("chain_domain", 987);
        password = node_->get_parameter_or<std::string>("password", "farmbot");

        crypto_ = std::make_shared<chain::Crypto>(private_key_file_);

        service_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        client_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        keysub_geoup_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        sub_options.callback_group = keysub_geoup_;
        qos_profile = rmw_qos_profile_services_default;
    }

    void setup() {
        chain_pub_ = node_->create_publisher<farmbot_interfaces::msg::Chain>("/chain", 10);

        beacon_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon/rci", 10, std::bind(&BlockyNode::genesisCreate, this, _1));
        beacons_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacons>(
            "/beacons/rci", 10, std::bind(&BlockyNode::beacons_callback, this, _1));
        chain_sub_ = node_->create_subscription<farmbot_interfaces::msg::Chain>(
            "/chain", 10, std::bind(&BlockyNode::chainCallback, this, _1));

        chain_publish_ = node_->create_wall_timer(1s, std::bind(&BlockyNode::chainPubT, this));

        join_service_ = node_->create_service<JoinChain>(
            "join_chain", std::bind(&BlockyNode::join_response, this, _1, _2), qos_profile, service_group_);

        vote_service_ = node_->create_service<VoteChain>(
            "vote_chain", std::bind(&BlockyNode::vote_response, this, _1, _2), qos_profile, service_group_);

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
    }

    void beacons_callback(const farmbot_interfaces::msg::Beacons::SharedPtr msg) {
        beacons_ = *msg;
        got_beacons_ = true;
    }

    void chainCallback(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        if (msg->uuid != std::to_string(chain_domain_)) {
            return;
        } else if (!chain_initialized_) {
            RCLCPP_INFO(node_->get_logger(), " *** [%s] Chain adopted from an existing /chain", namespace_.c_str());
            chain_ = chain::Chain(*msg);
            chain_initialized_ = true;
        } else if (!in_chain_ && got_beacons_ && got_beacon_) {
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
            in_chain_ = join_request(msg);
            if (!in_chain_) {
                RCLCPP_WARN(node_->get_logger(),
                            " -------------- Not allowed in the chain, thus quitting -------------------");
                rclcpp::shutdown();
            }
            chain_initialized_ = true;
        }
    }

    bool join_request(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        std::pair<int, int> m = getRandomMember(msg);
        std::string whom_to_ask = getNameFromUUID(msg->chain[m.first].transactions[m.second].uuid);
        target_permission_client_ =
            node_->create_client<JoinChain>("/" + whom_to_ask + "/join_chain", qos_profile, client_group_);
        EVP_PKEY *public_key =
            chain::loadPublicKeyFromPEM(getKeyStringFromUUID(msg->chain[m.first].transactions[m.second].uuid));

        RCLCPP_INFO(node_->get_logger(), " --- Asking robot >>> %s <<< to join the chain", whom_to_ask.c_str());
        auto request = std::make_shared<JoinChain::Request>();
        request->robot_uuid = beacon_.uuid;

        /// ----------- Encrypt password -----------
        std::vector<unsigned char> ciphertext = chain::encrypt(public_key, password);
        auto passwd_str = chain::base64Encode(ciphertext);
        request->encrypted_password = passwd_str;

        while (!target_permission_client_->wait_for_service(1s)) {
            if (!rclcpp::ok()) {
                RCLCPP_ERROR(node_->get_logger(), " -- Join service not available, node shutting down");
                return false;
            }
            RCLCPP_INFO(node_->get_logger(), " -- Waiting for service to appear...");
        }
        auto result_future = target_permission_client_->async_send_request(request);
        while (rclcpp::ok() && result_future.wait_for(1s) == std::future_status::timeout) {
            RCLCPP_INFO(node_->get_logger(), "Waiting for response from GPS2ENU service...");
        }
        auto result = result_future.get();
        std::string decoded_mes_sig_str = result->message_signature;
        std::vector<unsigned char> decoded_mes_sig_vec = chain::base64Decode(decoded_mes_sig_str);
        bool signature = chain::verify(public_key, result->message, decoded_mes_sig_vec);
        if (!signature) {
            RCLCPP_WARN(node_->get_logger(), " -- Join failed: invalid signature");
            return false;
        } else if (!result->success) {
            RCLCPP_WARN(node_->get_logger(), " -- %s:", result->message.c_str());
            return false;
        } else {
            chain_ = chain::Chain(result->chain);
            RCLCPP_INFO(node_->get_logger(), " -- Robot >>> %s <<< allowed us to join the chain", whom_to_ask.c_str());
            return true;
        }
    }

    void join_response(const std::shared_ptr<JoinChain::Request> req, std::shared_ptr<JoinChain::Response> res) {
        RCLCPP_INFO(node_->get_logger(), " -- Robot %s wants to join the chain", req->robot_uuid.c_str());
        /// ----------- Decrypt password -----------
        std::string password_enc = req->encrypted_password;
        std::vector<unsigned char> ciphertextBinary = chain::base64Decode(password_enc);
        std::vector<unsigned char> decrypted = crypto_->decrypt(ciphertextBinary);
        std::string password_str = chain::vectorToString(decrypted);
        if (password_str != password) {
            res->success = false;
            res->message = "Join failed: password is not valid";
        } else if (!chainConsensus(req->robot_uuid.c_str())) {
            res->success = false;
            res->message = "Join failed: chain consensus not reached";
        } else {
            chain_.addBlock(req->robot_uuid, "harvester", crypto_);
            res->success = true;
            res->chain = chain_.toMsg();
            res->message = "Allowing " + req->robot_uuid + " to join the chain";
        }
        auto message_signed = crypto_->sign(res->message);
        res->message_signature = chain::base64Encode(message_signed);
    }

    bool chainConsensus(std::string robot_to_join_uuid) {
        if (chain_.blocks_.size() < 2) {
            return true;
        }
        // if more than just the genesis block is in the chain, then then ask everyone in the chain to vote
        std::pair<int, int> vote_count = {0, 0};
        for (uint i = 0; i < chain_.blocks_.size(); i++) {
            std::string the_uuid = chain_.blocks_[i].transactions_[0].uuid_;
            if (the_uuid == beacon_.uuid) {
                continue;
            }
            vote_count.first++;
            auto who_to_ask = getNameFromUUID(chain_.blocks_[i].transactions_[0].uuid_);
            target_vote_client_ =
                node_->create_client<VoteChain>("/" + who_to_ask + "/vote_chain", qos_profile, client_group_);
            EVP_PKEY *public_key =
                chain::loadPublicKeyFromPEM(getKeyStringFromUUID(chain_.blocks_[i].transactions_[0].uuid_));
            RCLCPP_INFO(node_->get_logger(), " *** Asking robot >>> %s <<< to vote", who_to_ask.c_str());
            auto request = std::make_shared<VoteChain::Request>();
            request->robot_uuid = beacon_.uuid;
            request->robot_to_join_uuid = robot_to_join_uuid;

            while (!target_vote_client_->wait_for_service(1s)) {
                if (!rclcpp::ok()) {
                    RCLCPP_ERROR(node_->get_logger(), " -- Vote service not available, node shutting down");
                    return true;
                }
                RCLCPP_INFO(node_->get_logger(), " -- Waiting for service to appear...");
            }
            auto result_future = target_vote_client_->async_send_request(request);
            while (rclcpp::ok() && result_future.wait_for(1s) == std::future_status::timeout) {
                RCLCPP_INFO(node_->get_logger(), "Waiting for response from GPS2ENU service...");
            }
            auto result = result_future.get();
            std::string decoded_mes_sig_str = result->message_signature;
            std::vector<unsigned char> decoded_mes_sig_vec = chain::base64Decode(decoded_mes_sig_str);
            bool signature = chain::verify(public_key, result->message, decoded_mes_sig_vec);
            if (!signature) {
                RCLCPP_WARN(node_->get_logger(), " -- Voting failed: invalid signature");
            } else if (!result->vote) {
                RCLCPP_WARN(node_->get_logger(), " -- [%s] voted NO, reason: %s", who_to_ask.c_str(),
                            result->message.c_str());
            } else {
                RCLCPP_INFO(node_->get_logger(), " -- [%s] voted YES, reason: %s", who_to_ask.c_str(),
                            result->message.c_str());
                vote_count.second++;
            }
        }
        return (vote_count.second >= vote_count.first);
    }

    void vote_response(const std::shared_ptr<VoteChain::Request> req, std::shared_ptr<VoteChain::Response> res) {
        RCLCPP_INFO(node_->get_logger(), " -- Robot %s is asking us to vote", req->robot_uuid.c_str());
        if (!vote_policy_) {
            res->vote = false;
            res->message = "I, robot " + beacon_.uuid + ", am voting NO for: " + req->robot_uuid + " to join the chain";
        } else {
            res->message =
                "I, robot " + beacon_.uuid + ", am voting YES for: " + req->robot_uuid + " to join the chain";
            res->vote = true;
        }
        auto message_signed = crypto_->sign(res->message);
        res->message_signature = chain::base64Encode(message_signed);
    }

  private:
    std::pair<int, int> getRandomMember(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        if (msg == nullptr || msg->chain.empty()) {
            return {0, 0};
        }
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<std::size_t> chainDist(0, msg->chain.size() - 1);
        std::size_t randomChainIndex = chainDist(rng);
        if (msg->chain[randomChainIndex].transactions.empty()) {
            return {randomChainIndex, 0};
        }
        std::uniform_int_distribution<std::size_t> transDist(0, msg->chain[randomChainIndex].transactions.size() - 1);
        std::size_t randomTransIndex = transDist(rng);
        return {randomChainIndex, randomTransIndex};
    }
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

    std::string getKeyStringFromUUID(const std::string &uuid) {
        std::string key;
        for (const auto &beacon : beacons_.beacons) {
            if (beacon.uuid == uuid) {
                key = beacon.public_key;
                break;
            }
        }
        return key;
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
