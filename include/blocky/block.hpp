#include <iomanip>
#include <iostream>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <vector>

#include "transaction.hpp"
#include <farmbot_interfaces/msg/block.hpp>

class Block : public farmbot_interfaces::msg::Block {
  private:
    std::vector<Transaction> transactions_;

  public:
    Block(int64_t idx, std::string prev_hash, std::vector<Transaction> txns) {
        this->index = idx;
        this->previous_hash = prev_hash;
        for (const auto &txn : txns) {
            this->transactions.push_back(txn);
            this->transactions_.push_back(txn);
        }
        this->nonce = 0;
        this->timestamp.sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        this->timestamp.nanosec =
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() % 1000000000;
        // hash = calculateHash();
    }

    // Method to calculate the hash of the block
    std::string calculateHash() const {
        std::stringstream ss;
        ss << index << this->timestamp.sec << this->timestamp.nanosec << previous_hash << nonce;
        for (const auto &txn : transactions_) {
            ss << txn.toString();
        }
        return sha256(ss.str());
    }
    //
    // Proof of Work: Simple mining function
    void mineBlock(int difficulty) {
        std::string target(difficulty, '0');
        while (hash.substr(0, difficulty) != target) {
            nonce++;
            hash = calculateHash();
        }
        std::cout << "Block mined: " << hash << std::endl;
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
