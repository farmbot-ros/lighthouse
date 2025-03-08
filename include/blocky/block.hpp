#include <iomanip>
#include <iostream>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <vector>

#include "transaction.hpp"
#include <farmbot_interfaces/msg/block.hpp>

namespace chain {
    class Block : public farmbot_interfaces::msg::Block {
      public:
        int64_t index_;
        std::string previous_hash_;
        std::string hash_;
        std::vector<Transaction> transactions_;
        int64_t nonce_;
        builtin_interfaces::msg::Time timestamp_;

        Block() = default;
        Block(const farmbot_interfaces::msg::Block &msg) { fromMsg(msg); }
        Block(int64_t idx, std::string prev_hash, std::vector<Transaction> txns) {
            index_ = idx;
            previous_hash_ = prev_hash;
            transactions_ = txns;
            nonce_ = 0;
            timestamp_.sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            timestamp_.nanosec =
                duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() % 1000000000;
            hash_ = calculateHash();
        }

        // Method to calculate the hash of the block
        std::string calculateHash() const {
            std::stringstream ss;
            ss << index_ << timestamp_.sec << timestamp_.nanosec << previous_hash_ << nonce_;
            for (const auto &txn : transactions_) {
                ss << txn.toString();
            }
            return sha256(ss.str());
        }

        bool isValid() const {
            if (index_ < 0 || previous_hash_.empty() || hash_.empty()) {
                return false;
            }
            auto sec_now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            if (sec_now - timestamp_.sec < 10) {
                return false;
            }
            return true;
        }

        farmbot_interfaces::msg::Block toMsg() const {
            farmbot_interfaces::msg::Block msg;
            msg.index = index_;
            msg.previous_hash = previous_hash_;
            msg.hash = hash_;
            for (const auto &txn : transactions_) {
                msg.transactions.push_back(txn.toMsg());
            }
            msg.nonce = nonce_;
            msg.timestamp = timestamp_;
            return msg;
        }

        void fromMsg(const farmbot_interfaces::msg::Block &msg) {
            index_ = msg.index;
            previous_hash_ = msg.previous_hash;
            hash_ = msg.hash;
            for (const auto &transaction : msg.transactions) {
                Transaction txn;
                txn.fromMsg(transaction);
                transactions_.push_back(txn);
            }
            nonce_ = msg.nonce;
            timestamp_ = msg.timestamp;
        }

      private:
        // Helper function to calculate SHA-256 hash
        std::string sha256(const std::string &data) const {
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256((unsigned char *)data.c_str(), data.size(), hash);
            std::stringstream ss;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            return ss.str();
        }

        // Helper function to get the current timestamp
        std::string getCurrentTime() const {
            std::time_t now = std::time(nullptr);
            char buf[80];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            return std::string(buf);
        }
    };
} // namespace chain
