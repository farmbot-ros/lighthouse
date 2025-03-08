#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace chain {

    // Function to read a PEM file and return its content as a string.
    inline std::string pemToString(const std::string &filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Function to write a string (containing PEM data) back to a file.
    inline void stringToPem(const std::string &pemString, const std::string &filePath) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }
        file << pemString;
    }

    //-------------------------------------------------
    // Class for operations using the private key.
    // (Signing and Decryption)
    //-------------------------------------------------
    class OpenSSLPrivate {
      public:
        // Default constructor.
        OpenSSLPrivate() = default;

        // New constructor that loads a private key from a PEM string.
        // Pass true for 'isPEMString' to indicate the input is a PEM-formatted string.
        // or false to indicate the input is a file path.
        OpenSSLPrivate(const std::string &pemData, bool isPEMString = false) {
            if (isPEMString) {
                BIO *bio = BIO_new_mem_buf(pemData.data(), static_cast<int>(pemData.size()));
                if (!bio) {
                    throw std::runtime_error("Unable to create BIO from PEM string");
                }
                pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
                BIO_free(bio);
                if (!pkey) {
                    throw std::runtime_error("Unable to load private key from PEM string");
                }
            } else {
                // Fallback: treat pemData as a file path.
                FILE *keyFile = fopen(pemData.c_str(), "r");
                if (!keyFile) {
                    throw std::runtime_error("Unable to open private key file: " + pemData);
                }
                pkey = PEM_read_PrivateKey(keyFile, nullptr, nullptr, nullptr);
                fclose(keyFile);
                if (!pkey) {
                    throw std::runtime_error("Unable to read private key from file: " + pemData);
                }
            }
        }

        ~OpenSSLPrivate() {
            if (pkey) {
                EVP_PKEY_free(pkey);
            }
        }

        // Signs a string (using SHA-256) and returns the signature as bytes.
        std::vector<unsigned char> sign(const std::string &data) {
            EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
            if (!mdctx) {
                throw std::runtime_error("Unable to create EVP_MD_CTX for signing");
            }
            if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignInit failed");
            }
            if (EVP_DigestSignUpdate(mdctx, data.c_str(), data.size()) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignUpdate failed");
            }
            size_t sig_len = 0;
            if (EVP_DigestSignFinal(mdctx, nullptr, &sig_len) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignFinal (get length) failed");
            }
            std::vector<unsigned char> signature(sig_len);
            if (EVP_DigestSignFinal(mdctx, signature.data(), &sig_len) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestSignFinal (signing) failed");
            }
            signature.resize(sig_len);
            EVP_MD_CTX_free(mdctx);
            return signature;
        }

        // Decrypts ciphertext (bytes) and returns the plaintext as bytes.
        std::vector<unsigned char> decrypt(const std::vector<unsigned char> &ciphertext) {
            EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
            if (!ctx) {
                throw std::runtime_error("Unable to create EVP_PKEY_CTX for decryption");
            }
            if (EVP_PKEY_decrypt_init(ctx) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_decrypt_init failed");
            }
            size_t outlen = 0;
            if (EVP_PKEY_decrypt(ctx, nullptr, &outlen, ciphertext.data(), ciphertext.size()) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_decrypt (get length) failed");
            }
            std::vector<unsigned char> plaintext(outlen);
            if (EVP_PKEY_decrypt(ctx, plaintext.data(), &outlen, ciphertext.data(), ciphertext.size()) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_decrypt failed");
            }
            plaintext.resize(outlen);
            EVP_PKEY_CTX_free(ctx);
            return plaintext;
        }

        // Convenience overload: decrypt ciphertext and return as string.
        std::string decryptToString(const std::vector<unsigned char> &ciphertext) {
            std::vector<unsigned char> plaintext = decrypt(ciphertext);
            return std::string(plaintext.begin(), plaintext.end());
        }

      private:
        EVP_PKEY *pkey = nullptr;
    };

    //-------------------------------------------------
    // Class for operations using the public key.
    // (Verification and Encryption)
    //-------------------------------------------------
    class OpenSSLPublic {
      public:
        // Default constructor.
        OpenSSLPublic() = default;

        // New constructor that loads a public key from a PEM string.
        // Pass true for 'isPEMString' to indicate the input is a PEM-formatted string.
        // or false to indicate the input is a file path.
        OpenSSLPublic(const std::string &pemData, bool isPEMString = false) {
            if (isPEMString) {
                BIO *bio = BIO_new_mem_buf(pemData.data(), static_cast<int>(pemData.size()));
                if (!bio) {
                    throw std::runtime_error("Unable to create BIO from PEM string");
                }
                pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
                BIO_free(bio);
                if (!pkey) {
                    throw std::runtime_error("Unable to load public key from PEM string");
                }
            } else {
                FILE *keyFile = fopen(pemData.c_str(), "r");
                if (!keyFile) {
                    throw std::runtime_error("Unable to open public key file: " + pemData);
                }
                pkey = PEM_read_PUBKEY(keyFile, nullptr, nullptr, nullptr);
                fclose(keyFile);
                if (!pkey) {
                    throw std::runtime_error("Unable to read public key from file: " + pemData);
                }
            }
        }

        ~OpenSSLPublic() {
            if (pkey) {
                EVP_PKEY_free(pkey);
            }
        }

        // Verifies that the signature matches the given data (using SHA-256).
        bool verify(const std::string &data, const std::vector<unsigned char> &signature) {
            EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
            if (!mdctx) {
                throw std::runtime_error("Unable to create EVP_MD_CTX for verification");
            }
            if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestVerifyInit failed");
            }
            if (EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.size()) != 1) {
                EVP_MD_CTX_free(mdctx);
                throw std::runtime_error("EVP_DigestVerifyUpdate failed");
            }
            int ret = EVP_DigestVerifyFinal(mdctx, signature.data(), signature.size());
            EVP_MD_CTX_free(mdctx);
            return (ret == 1);
        }

        // Encrypts plaintext (bytes) using the public key and returns ciphertext as bytes.
        std::vector<unsigned char> encrypt(const std::vector<unsigned char> &plaintext) {
            EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
            if (!ctx) {
                throw std::runtime_error("Unable to create EVP_PKEY_CTX for encryption");
            }
            if (EVP_PKEY_encrypt_init(ctx) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_encrypt_init failed");
            }
            size_t outlen = 0;
            if (EVP_PKEY_encrypt(ctx, nullptr, &outlen, plaintext.data(), plaintext.size()) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_encrypt (get length) failed");
            }
            std::vector<unsigned char> ciphertext(outlen);
            if (EVP_PKEY_encrypt(ctx, ciphertext.data(), &outlen, plaintext.data(), plaintext.size()) <= 0) {
                EVP_PKEY_CTX_free(ctx);
                throw std::runtime_error("EVP_PKEY_encrypt failed");
            }
            ciphertext.resize(outlen);
            EVP_PKEY_CTX_free(ctx);
            return ciphertext;
        }

        // Convenience overload: encrypts a string and returns ciphertext as bytes.
        std::vector<unsigned char> encrypt(const std::string &plaintextStr) {
            std::vector<unsigned char> plaintext(plaintextStr.begin(), plaintextStr.end());
            return encrypt(plaintext);
        }

      private:
        EVP_PKEY *pkey = nullptr;
    };

} // namespace chain

// //-----------------------------------------------
// // Example usage:
// int main() {
//     try {
//         // Instantiate the private key operations (for signing and decryption).
//         blocky::OpenSSLPrivate privateOps("private_key.pem");
//         // Instantiate the public key operations (for verification and encryption).
//         blocky::OpenSSLPublic publicOps("public_key.pem");
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
