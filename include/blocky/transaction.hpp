#include <ctime>
#include <farmbot_interfaces/msg/detail/transaction__struct.hpp>
#include <iostream>
#include <sstream>
#include <string>

#include <farmbot_interfaces/msg/transaction.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>

using namespace std::chrono;

namespace chain {
    class Transaction : public farmbot_interfaces::msg::Transaction {
      public:
        Transaction(int16_t priority, std::string uuid, std::string function) {
            this->priority = priority;
            this->uuid = uuid;
            this->function = function;
            this->timestamp.sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            this->timestamp.nanosec =
                duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() % 1000000000;
        }

        void signTransaction(const std::string &privateKey) {
            // TODO: Implement actual cryptographic signing using privateKey
            signature = "signed_with_" + privateKey;
        }

        // Method to verify the transaction signature
        bool isValid() const {
            if (uuid.empty() || function.empty() || signature.empty() || priority < 0 || priority > 255) {
                return false;
            }
            // TODO: Implement actual signature verification
            return true;
        }

        std::string toString() const {
            std::stringstream ss;
            ss << this->timestamp.sec << this->timestamp.nanosec << priority << uuid << function;
            return ss.str();
        }
    };
} // namespace chain
