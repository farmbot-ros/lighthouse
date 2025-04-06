#include <chrono>
#include <iostream>
#include <random>
#include <rclcpp/executors.hpp>
#include <rclcpp/subscription_options.hpp>
#include <sstream>
#include <std_msgs/msg/detail/string__struct.hpp>
#include <string>

#include "blocky/chain.hpp"

#include <farmbot_interfaces/msg/agent.hpp>
#include <farmbot_interfaces/msg/agents.hpp>
#include <farmbot_interfaces/msg/chain.hpp>
#include <farmbot_interfaces/srv/join_chain.hpp>
#include <farmbot_interfaces/srv/update_chain.hpp>
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

    farmbot_interfaces::msg::Agent beacon_;
    farmbot_interfaces::msg::Agents beacons_;
    std::vector<std::pair<std::string, std::string>> robot_passphrases_;
    std::string my_passphrase_;

    chain::Chain chain_;
    std::shared_ptr<chain::Crypto> crypto_;
    std::string private_key_file_;
    bool chain_initialized_, in_chain_, got_beacons_, got_beacon_;

    rclcpp::CallbackGroup::SharedPtr client_group_, service_group_;
    rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(10));

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;

    rclcpp::Subscription<farmbot_interfaces::msg::Agent>::SharedPtr beacon_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Agents>::SharedPtr beacons_sub_;

    rclcpp::TimerBase::SharedPtr chain_publish_;

    using JoinChain = farmbot_interfaces::srv::JoinChain;
    rclcpp::Service<JoinChain>::SharedPtr join_service_;
    rclcpp::Client<JoinChain>::SharedPtr target_permission_client_;

    using VoteChain = farmbot_interfaces::srv::VoteChain;
    rclcpp::Service<VoteChain>::SharedPtr vote_service_;
    rclcpp::Client<VoteChain>::SharedPtr target_vote_client_;

    using UpdateChain = farmbot_interfaces::srv::UpdateChain;
    rclcpp::Service<UpdateChain>::SharedPtr update_service_;
    rclcpp::Client<UpdateChain>::SharedPtr target_update_client_;

  public:
    BlockyNode(rclcpp::Node::SharedPtr node) : node_(node) {
        RCLCPP_INFO(node_->get_logger(), "BlockyNode started");
        // Namespace
        namespace_ = node_->get_namespace();
        if (!namespace_.empty() && namespace_[0] == '/') {
            namespace_.erase(0, 1);
        }

        private_key_file_ = node_->get_parameter_or<std::string>("private_key_file", "private_key.pem");
        RCLCPP_INFO(node_->get_logger(), " *** Private key file: %s", private_key_file_.c_str());
        chain_domain_ = node_->get_parameter_or<int32_t>("chain_domain", 987);
        password = node_->get_parameter_or<std::string>("password", "farmbot");
        my_passphrase_ = generateRandomPassphrase(10);

        crypto_ = std::make_shared<chain::Crypto>(private_key_file_);

        service_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        client_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    }

    void setup() {
        chain_pub_ = node_->create_publisher<farmbot_interfaces::msg::Chain>("/chain", 10);
        chain_publish_ = node_->create_wall_timer(1s, std::bind(&BlockyNode::chain_publisher, this));

        beacon_sub_ = node_->create_subscription<farmbot_interfaces::msg::Agent>(
            "beacon/rci", 10, std::bind(&BlockyNode::initialize_chain, this, _1));
        beacons_sub_ = node_->create_subscription<farmbot_interfaces::msg::Agents>(
            "/beacons/rci", 10, std::bind(&BlockyNode::beacons_callback, this, _1));
        chain_sub_ = node_->create_subscription<farmbot_interfaces::msg::Chain>(
            "/chain", 10, std::bind(&BlockyNode::all_chain_callback, this, _1));

        join_service_ = node_->create_service<JoinChain>(
            "join_chain", std::bind(&BlockyNode::join_response, this, _1, _2), qos, service_group_);
        vote_service_ = node_->create_service<VoteChain>(
            "vote_chain", std::bind(&BlockyNode::vote_response, this, _1, _2), qos, service_group_);
        update_service_ = node_->create_service<UpdateChain>(
            "update_chain", std::bind(&BlockyNode::update_response, this, _1, _2), qos, service_group_);

        RCLCPP_INFO(node_->get_logger(), "BeaconNode started");
    }

    void initialize_chain(const farmbot_interfaces::msg::Agent::SharedPtr msg) {
        if (!chain_initialized_) {
            RCLCPP_INFO(node_->get_logger(), " *** [%s] created GENESIS block and initialized the chain in domain [%s]",
                        namespace_.c_str(), std::to_string(chain_domain_).c_str());
            chain_ = chain::Chain(std::to_string(chain_domain_), msg->uuid, msg->participants[0].functions[0], crypto_);
            chain_initialized_ = true, in_chain_ = true;
        }
        beacon_ = *msg;
        got_beacon_ = true;
        beacon_sub_.reset();
    }

    void chain_publisher() {
        if (in_chain_) {
            chain_pub_->publish(chain_.toMsg());
        }
    }

    void beacons_callback(const farmbot_interfaces::msg::Agents::SharedPtr msg) {
        beacons_ = *msg;
        got_beacons_ = true;
    }

    void all_chain_callback(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        if (msg->uuid != std::to_string(chain_domain_)) {
            return;
        } else if (!chain_initialized_) {
            RCLCPP_INFO(node_->get_logger(), " *** [%s] Chain adopted from an existing /chain", namespace_.c_str());
            chain_ = chain::Chain(*msg);
            chain_initialized_ = true;
        } else if (!in_chain_ && got_beacons_ && got_beacon_) {
            for (const auto &block : chain_.blocks_) {
                for (const auto &transaction : block.transactions_) {
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
        auto the_uuid = msg->chain[m.first].transactions[m.second].uuid;
        std::string whom_to_ask = getNameFromUUID(the_uuid);
        target_permission_client_ =
            node_->create_client<JoinChain>("/" + whom_to_ask + "/join_chain", qos, client_group_);
        EVP_PKEY *public_key = chain::loadPublicKeyFromPEM(getKeyStringFromUUID(the_uuid));

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
            RCLCPP_INFO(node_->get_logger(), "Waiting for response from [JOIN] service...");
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
        } else if (!vote_request(req->robot_uuid.c_str())) {
            res->success = false;
            res->message = "Join failed: chain consensus not reached";
        } else {
            chain_.addBlock(req->robot_uuid, "harvester", crypto_);
            res->success = true;
            res->chain = chain_.toMsg();
            res->message = "Allowing " + req->robot_uuid + " to join the chain";
            // notify the blockchain that a new robot has joined
            update_request();
        }
        auto message_signed = crypto_->sign(res->message);
        res->message_signature = chain::base64Encode(message_signed);
    }

    bool vote_request(std::string robot_to_join_uuid) {
        if (chain_.blocks_.size() < 2) {
            return true;
        }
        std::pair<int, int> vote_count = {0, 0};
        for (uint i = 0; i < chain_.blocks_.size(); i++) {
            vote_count.first++;
            std::string the_uuid = chain_.blocks_[i].transactions_[0].uuid_;
            if (the_uuid == beacon_.uuid) {
                if (vote_policy_) {
                    vote_count.second++;
                }
                continue;
            }
            auto who_to_ask = getNameFromUUID(the_uuid);
            EVP_PKEY *public_key = chain::loadPublicKeyFromPEM(getKeyStringFromUUID(the_uuid));
            target_vote_client_ = node_->create_client<VoteChain>("/" + who_to_ask + "/vote_chain", qos, client_group_);
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
                RCLCPP_INFO(node_->get_logger(), "Waiting for response from [VOTE] service...");
            }
            auto result = result_future.get();
            std::string decoded_mes_sig_str = result->message_signature;
            std::vector<unsigned char> decoded_mes_sig_vec = chain::base64Decode(decoded_mes_sig_str);
            bool signature = chain::verify(public_key, result->message, decoded_mes_sig_vec);
            if (result->vote && signature == true) {
                vote_count.second++;
            }

            std::string passphrase_enc = result->encrypted_phrase;
            std::vector<unsigned char> ciphertextBinary = chain::base64Decode(passphrase_enc);
            std::vector<unsigned char> decrypted = crypto_->decrypt(ciphertextBinary);
            std::string passphrase_str = chain::vectorToString(decrypted);
            robot_passphrases_.push_back(std::make_pair(the_uuid, passphrase_str));
        }
        if (vote_policy_) {
            vote_count.first++;
            vote_count.second++;
        }
        RCLCPP_INFO(node_->get_logger(), " ------------>> With %d votes from %d total members, decision is %s",
                    vote_count.second, vote_count.first, vote_count.second >= vote_count.first ? "YES" : "NO");
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

        EVP_PKEY *public_key = chain::loadPublicKeyFromPEM(getKeyStringFromUUID(req->robot_uuid));
        std::vector<unsigned char> ciphertext = chain::encrypt(public_key, my_passphrase_);
        auto passwd_str = chain::base64Encode(ciphertext);
        res->encrypted_phrase = passwd_str;
    }

    void update_request() {
        for (uint i = 0; i < robot_passphrases_.size(); i++) {
            auto the_uuid = robot_passphrases_[i].first;
            auto the_passphrase = robot_passphrases_[i].second;
            std::string whom_to_ask = getNameFromUUID(the_uuid);
            target_update_client_ =
                node_->create_client<UpdateChain>("/" + whom_to_ask + "/update_chain", qos, client_group_);
            RCLCPP_INFO(node_->get_logger(), " *** Asking robot >>> %s <<< to update the chain", whom_to_ask.c_str());
            auto request = std::make_shared<UpdateChain::Request>();

            auto public_key = chain::loadPublicKeyFromPEM(getKeyStringFromUUID(the_uuid));
            std::vector<unsigned char> ciphertext = chain::encrypt(public_key, the_passphrase);
            auto passwd_str = chain::base64Encode(ciphertext);
            request->encrypted_passphrase = passwd_str;
            request->chain = chain_.toMsg();
            while (!target_update_client_->wait_for_service(1s)) {
                if (!rclcpp::ok()) {
                    RCLCPP_ERROR(node_->get_logger(), " -- Update service not available, node shutting down");
                    return;
                }
                RCLCPP_INFO(node_->get_logger(), " -- Waiting for service to appear...");
            }
            auto result_future = target_update_client_->async_send_request(request);
            while (rclcpp::ok() && result_future.wait_for(1s) == std::future_status::timeout) {
                RCLCPP_INFO(node_->get_logger(), "Waiting for response from [UPDATE] service...");
            }
            auto result = result_future.get();
            if (result->success) {
                RCLCPP_INFO(node_->get_logger(), " -- Robot %s updated the chain", the_uuid.c_str());
            }
        }
    }

    void update_response(const std::shared_ptr<UpdateChain::Request> req, std::shared_ptr<UpdateChain::Response> res) {
        RCLCPP_INFO(node_->get_logger(), " -- Update chain signal recieved ... checking validity");
        std::string passphrase_enc = req->encrypted_passphrase;
        std::vector<unsigned char> ciphertextBinary = chain::base64Decode(passphrase_enc);
        std::vector<unsigned char> decrypted = crypto_->decrypt(ciphertextBinary);
        std::string passphrase_str = chain::vectorToString(decrypted);
        if (passphrase_str != my_passphrase_) {
            res->success = false;
        } else {
            res->success = true;
            chain_ = chain::Chain(req->chain);
            RCLCPP_INFO(node_->get_logger(), " -- Chain updated");
        }
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
        for (const auto &beacon : beacons_.agents) {
            if (beacon.uuid == uuid) {
                name = beacon.name;
                break;
            }
        }
        return name;
    }
    std::string getKeyStringFromUUID(const std::string &uuid) {
        std::string key;
        for (const auto &beacon : beacons_.agents) {
            if (beacon.uuid == uuid) {
                key = beacon.public_key;
                break;
            }
        }
        return key;
    }

    std::string generateRandomPassphrase(int length) {
        const std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string passphrase;
        for (int i = 0; i < length; ++i) {
            int randomIndex = std::rand() % characters.size();
            passphrase += characters[randomIndex];
        }
        return passphrase;
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
