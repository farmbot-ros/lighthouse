#include <iostream>
#include <vector>

#include "block.hpp"
#include <farmbot_interfaces/msg/chain.hpp>

namespace chain {
    class Chain : public farmbot_interfaces::msg::Chain {
      public:
        std::string uuid_;
        builtin_interfaces::msg::Time timestamp_;
        std::vector<Block> blocks_;

        Chain() = default;
        Chain(const farmbot_interfaces::msg::Chain &msg) { fromMsg(msg); }
        Chain(std::string s_uuid, std::string t_uuid, std::string function, std::shared_ptr<chain::Crypto> privateKey_,
              int16_t priority = 100) {
            Transaction genesisTransaction(t_uuid, function, priority);
            genesisTransaction.signTransaction(privateKey_);
            Block genesisBlock({genesisTransaction});
            blocks_.push_back(genesisBlock);
            uuid_ = s_uuid;
        }

        Chain(std::string s_uuid, std::string t_uuid, std::string function, int16_t priority = 100) {
            Transaction genesisTransaction(t_uuid, function, priority);
            Block genesisBlock({genesisTransaction});
            blocks_.push_back(genesisBlock);
            uuid_ = s_uuid;
        }

        // Method to add a new block to the blockchain
        void addBlock(const Block &newBlock) {
            Block blockToAdd = newBlock;
            blockToAdd.previous_hash_ = blocks_.back().hash_;
            blockToAdd.index_ = blocks_.back().index_ + 1;
            if (!blockToAdd.isValid()) {
                std::cout << "Invalid block attempted to be added to the blockchain" << std::endl;
                return;
            }
            std::cout << "Adding block to chain" << std::endl;
            blocks_.push_back(blockToAdd);
        }

        void addBlock(std::string uuid, std::string function, std::shared_ptr<chain::Crypto> privateKey_,
                      int16_t priority = 100) {
            Transaction genesisTransaction(uuid, function, priority);
            genesisTransaction.signTransaction(privateKey_);
            Block genesisBlock({genesisTransaction});
            addBlock(genesisBlock);
        }

        // Method to validate the integrity of the blockchain
        bool isValid() const {
            if (blocks_.empty()) {
                return false;
            }
            if (blocks_.size() == 1) {
                return true;
            }
            for (size_t i = 1; i < blocks_.size(); i++) {
                const Block &currentBlock_ = blocks_[i];
                const Block &previousBlock = blocks_[i - 1];

                // Check if the current block's hash is correct
                if (currentBlock_.hash_ != currentBlock_.calculateHash()) {
                    return false;
                }

                // Check if the current block's previous hash matches the hash of the previous block
                if (currentBlock_.previous_hash_ != previousBlock.hash_) {
                    return false;
                }
            }
            return true;
        }

        farmbot_interfaces::msg::Chain toMsg() const {
            farmbot_interfaces::msg::Chain msg;
            msg.timestamp = timestamp_;
            msg.uuid = uuid_;
            for (const auto &block : blocks_) {
                msg.chain.push_back(block.toMsg());
            }
            return msg;
        }

        void fromMsg(const farmbot_interfaces::msg::Chain &msg) {
            timestamp_ = msg.timestamp;
            uuid_ = msg.uuid;
            for (const auto &block : msg.chain) {
                Block blk;
                blk.fromMsg(block);
                blocks_.push_back(blk);
            }
        }
    };
} // namespace chain
