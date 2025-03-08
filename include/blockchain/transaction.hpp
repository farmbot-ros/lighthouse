#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

#include <chrono>

using namespace std::chrono;

class Transaction {
  public:
    int32_t sec;
    int32_t nanossec;
    int16_t priority;
    std::string uuid;
    std::string name;
    std::string color;
    std::string function;
    std::string signature;

    Transaction(int16_t priority, std::string uuid, std::string name, std::string color, std::string function)
        : priority(priority), uuid(uuid), name(name), color(color), function(function) {
        sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        nanossec = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() % 1000000000;
        signature = "";
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
        ss << sec << nanossec << priority << uuid << name << color << function;
        return ss.str();
    }
};
