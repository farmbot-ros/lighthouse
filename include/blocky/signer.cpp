#include <cstdio>
#include <cstring>
#include <iostream>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace blocky {
    class OpenSSLSigner {
      public:
        // Constructor loads the private key from a PEM file
        OpenSSLSigner(const std::string &privateKeyPath) {
            FILE *keyFile = fopen(privateKeyPath.c_str(), "r");
            if (!keyFile) {
                throw std::runtime_error("Unable to open private key file: " + privateKeyPath);
            }
            pkey = PEM_read_PrivateKey(keyFile, nullptr, nullptr, nullptr);
            fclose(keyFile);
            if (!pkey) {
                throw std::runtime_error("Unable to read private key from file: " + privateKeyPath);
            }
        }

        // Destructor frees the private key
        ~OpenSSLSigner() {
            if (pkey) {
                EVP_PKEY_free(pkey);
            }
        }

        // sign() takes data as a string and returns the signature as a vector of bytes
        std::vector<unsigned char> sign(const std::string &data) {
            EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
            if (!mdctx) {
                throw std::runtime_error("Unable to create EVP_MD_CTX");
            }

            // Initialize the signing context using SHA-256
            if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignInit failed");
            }

            // Feed data into the signing context
            if (EVP_DigestSignUpdate(mdctx, data.c_str(), data.size()) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignUpdate failed");
            }

            // Determine the required signature length
            size_t sig_len = 0;
            if (EVP_DigestSignFinal(mdctx, nullptr, &sig_len) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignFinal (get length) failed");
            }

            // Allocate buffer for the signature
            std::vector<unsigned char> signature(sig_len);
            if (EVP_DigestSignFinal(mdctx, signature.data(), &sig_len) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignFinal (actual signing) failed");
            }
            signature.resize(sig_len); // adjust vector size to the actual signature length

            EVP_MD_CTX_free(mdctx);
            return signature;
        }

      private:
        EVP_PKEY *pkey = nullptr;
    };
} // namespace blocky

// // Example usage:
// int main() {
//     try {
//         // Create an instance of the signer with the path to your private key
//         OpenSSLSigner signer("private_key.pem");
//         std::string data = "This is the data to sign";
//
//         // Sign the data
//         std::vector<unsigned char> signature = signer.sign(data);
//         std::cout << "Signature generated, length: " << signature.size() << "\n";
//
//         // Optionally, print the signature in hexadecimal format
//         for (unsigned char byte : signature) {
//             printf("%02x", byte);
//         }
//         printf("\n");
//     } catch (const std::exception &ex) {
//         std::cerr << "Error: " << ex.what() << "\n";
//         return 1;
//     }
//
//     return 0;
// }
