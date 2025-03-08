#include <iostream>
#include <vector>

#include "block.hpp"
#include <farmbot_interfaces/msg/chain.hpp>

namespace chain {
    class Chain : public farmbot_interfaces::msg::Chain {
      public:
        std::vector<Block> chain_;
        int difficulty;

        // Constructor: Initialize the blockchain with the genesis block
        Chain(int16_t priority, std::string uuid, std::string function) {
            Transaction genesisTransaction(priority, uuid, function);
            Block genesisBlock(0, "0", {genesisTransaction});
            chain.push_back(genesisBlock);
            chain_.push_back(genesisBlock);
        }

        Chain(const std::vector<Block> &chain) : chain_(chain) {
            for (const auto &block : chain_) {
                this->chain.push_back(block);
            }
        }

        // Method to add a new block to the blockchain
        void addBlock(const Block &newBlock) {
            Block blockToAdd = newBlock;
            blockToAdd.previous_hash = this->chain.back().hash;
            if (!blockToAdd.isValid()) {
                std::cout << "Invalid block attempted to be added to the blockchain" << std::endl;
                return;
            }
            chain.push_back(blockToAdd);
            chain_.push_back(blockToAdd);
        }

        // Method to validate the integrity of the blockchain
        bool isChainValid() const {
            for (size_t i = 1; i < chain.size(); i++) {
                // const farmbot_interfaces::msg::Block &currentBlock = chain[i];
                const farmbot_interfaces::msg::Block &previousBlock = chain[i - 1];
                const Block &currentBlock_ = chain_[i];
                // const Block &previousBlock_ = chain_[i - 1];

                // Check if the current block's hash is correct
                if (currentBlock_.hash != currentBlock_.calculateHash()) {
                    return false;
                }

                // Check if the current block's previous hash matches the hash of the previous block
                if (currentBlock_.previous_hash != previousBlock.hash) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace chain
