#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "blocky/chain.hpp"

#include <farmbot_interfaces/msg/beacon.hpp>
#include <farmbot_interfaces/msg/chain.hpp>
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

    chain::Chain chain_;
    std::shared_ptr<chain::Crypto> crypto_;
    std::string private_key_file_;
    bool chain_initialized_;
    bool is_genesis_;
    bool in_chain_;

    rclcpp::Publisher<farmbot_interfaces::msg::Chain>::SharedPtr chain_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr public_key_pub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Chain>::SharedPtr chain_sub_;
    rclcpp::Subscription<farmbot_interfaces::msg::Beacon>::SharedPtr beacon_sub_;

    rclcpp::TimerBase::SharedPtr chain_publish_;

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
    }

    void setup() {
        chain_pub_ = node_->create_publisher<farmbot_interfaces::msg::Chain>("/chain", 10);
        public_key_pub_ = node_->create_publisher<std_msgs::msg::String>("public_key", 10);

        beacon_sub_ = node_->create_subscription<farmbot_interfaces::msg::Beacon>(
            "beacon/rci", 10, std::bind(&BlockyNode::beacon_callback, this, _1));
        chain_sub_ = node_->create_subscription<farmbot_interfaces::msg::Chain>(
            "/chain", 10, std::bind(&BlockyNode::chain_callback, this, _1));

        chain_publish_ = node_->create_wall_timer(1s, std::bind(&BlockyNode::chain_publish_timer_callback, this));

        RCLCPP_INFO(node_->get_logger(), "BeaconNode started");
    }

    void beacon_callback(const farmbot_interfaces::msg::Beacon::SharedPtr msg) {
        if (!chain_initialized_) {
            // genesis block
            chain_ = chain::Chain(std::to_string(chain_domain_), msg->priority, msg->uuid, msg->function, crypto_);
            chain_initialized_ = true, is_genesis_ = true, in_chain_ = true;
        }
        beacon_ = *msg;
        beacon_sub_.reset();
    }
    void chain_callback(const farmbot_interfaces::msg::Chain::SharedPtr msg) {
        if (msg->uuid != std::to_string(chain_domain_)) {
            return;
        }
        if (!chain_initialized_) {
            chain_ = chain::Chain(*msg);
            chain_initialized_ = true;
        }
    }

    void chain_publish_timer_callback() {
        if (in_chain_) {
            chain_pub_->publish(chain_.toMsg());
        }
        std_msgs::msg::String public_key_msg;
        public_key_msg.data = crypto_->getPublicHalf();
        public_key_pub_->publish(public_key_msg);
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

//-----------------------------------------------
// Example usage:
// int main() {
//     try {
//         // Instantiate the private key operations (for signing and decryption).
//         chain::OpenSSLPrivate privateOps("private_key.pem");
//         // Instantiate the public key operations (for verification and encryption).
//         chain::OpenSSLPublic publicOps("public_key.pem");
//
//         // ----- Signing & Verification -----
//         std::string dataToSign = "This is the data to sign";
//         std::vector<unsigned char> signature = privateOps.sign(dataToSign);
//         std::cout << "Signature generated, length: " << signature.size() << "\n";
//         for (unsigned char byte : signature) {
//             printf("%02x", byte);
//         }
//         printf("\n");
//
//         bool valid = publicOps.verify(dataToSign, signature);
//         std::cout << (valid ? "Signature verified successfully." : "Signature verification failed.") << "\n";
//
//         // ----- Encryption & Decryption -----
//         std::string message = "Hello, World!";
//         // Encrypt the message using the public key.
//         std::vector<unsigned char> ciphertext = publicOps.encrypt(message);
//         std::cout << "Encryption complete, ciphertext length: " << ciphertext.size() << "\n";
//         for (unsigned char byte : ciphertext) {
//             printf("%02x", byte);
//         }
//         printf("\n");
//
//         // Decrypt the ciphertext using the private key.
//         std::string decryptedMessage = privateOps.decryptToString(ciphertext);
//         std::cout << "Decryption complete, plaintext: " << decryptedMessage << "\n";
//     } catch (const std::exception &ex) {
//         std::cerr << "Error: " << ex.what() << "\n";
//         return 1;
//     }
//     return 0;
// }
