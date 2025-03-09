#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

#include <farmbot_interfaces/msg/transaction.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>

#include "signer.hpp"

using namespace std::chrono;

namespace chain {
    struct Timestamp {
        int32_t sec;
        uint32_t nanosec;
        void toMsg(builtin_interfaces::msg::Time &msg) const {
            msg.sec = sec;
            msg.nanosec = nanosec;
        }
        void fromMsg(const builtin_interfaces::msg::Time &msg) {
            sec = msg.sec;
            nanosec = msg.nanosec;
        }
    };

    class Transaction {
      public:
        Timestamp timestamp_;
        int16_t priority_;
        std::string uuid_;
        std::string function_;
        std::vector<unsigned char> signature_;

        Transaction() = default;
        Transaction(const farmbot_interfaces::msg::Transaction &msg) { fromMsg(msg); }
        Transaction(std::string uuid, std::string function, int16_t priority = 100) {
            priority_ = priority;
            uuid_ = uuid;
            function_ = function;
            timestamp_.sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            timestamp_.nanosec =
                duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() % 1000000000;
        }

        void signTransaction(std::shared_ptr<chain::Crypto> privateKey_) { signature_ = privateKey_->sign(toString()); }

        // bool verifyTransaction(std::shared_ptr<chain::OpenSSLPublic> publicKey_) {
        // return publicKey_->verify(toString(), signature_);
        // }

        // Method to verify the transaction signature
        bool isValid() const {
            if (uuid_.empty() || function_.empty() || signature_.empty() || priority_ < 0 || priority_ > 255) {
                return false;
            }
            // TODO: Implement actual signature verification
            return true;
        }

        std::string toString() const {
            std::stringstream ss;
            ss << timestamp_.sec << timestamp_.nanosec << priority_ << uuid_ << function_;
            return ss.str();
        }

        farmbot_interfaces::msg::Transaction toMsg() const {
            farmbot_interfaces::msg::Transaction msg;
            msg.priority = priority_;
            msg.uuid = uuid_;
            msg.function = function_;
            msg.signature = signature_;
            return msg;
        }

        void fromMsg(const farmbot_interfaces::msg::Transaction &msg) {
            timestamp_.fromMsg(msg.timestamp);
            priority_ = msg.priority;
            uuid_ = msg.uuid;
            function_ = msg.function;
            signature_ = msg.signature;
        }
    };
} // namespace chain
