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

    // Helper functions for PEM file/string operations.
    inline std::string pemToString(const std::string &filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    inline void stringToPem(const std::string &pemString, const std::string &filePath) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }
        file << pemString;
    }

    inline std::vector<unsigned char> stringToVector(const std::string &str) {
        return std::vector<unsigned char>(str.begin(), str.end());
    }

    inline std::string vectorToString(const std::vector<unsigned char> &vec) {
        return std::string(vec.begin(), vec.end());
    }

    inline std::string base64Encode(const std::vector<unsigned char> &data) {
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;

        // Create a Base64 filter BIO.
        b64 = BIO_new(BIO_f_base64());
        // Create a memory BIO.
        bio = BIO_new(BIO_s_mem());
        // Chain the Base64 BIO on top of the memory BIO.
        bio = BIO_push(b64, bio);

        // Optionally disable newlines in the output.
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

        // Write the data into the BIO chain.
        if (BIO_write(bio, data.data(), data.size()) <= 0) {
            BIO_free_all(bio);
            throw std::runtime_error("BIO_write failed during Base64 encoding");
        }
        if (BIO_flush(bio) != 1) {
            BIO_free_all(bio);
            throw std::runtime_error("BIO_flush failed during Base64 encoding");
        }

        // Get a pointer to the memory BIO's data.
        BIO_get_mem_ptr(bio, &bufferPtr);
        std::string encoded(bufferPtr->data, bufferPtr->length);

        // Free all BIO resources.
        BIO_free_all(bio);
        return encoded;
    }

    inline std::vector<unsigned char> base64Decode(const std::string &encoded) {
        BIO *bio, *b64;
        // Create a memory BIO from the encoded string.
        bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
        // Create a Base64 filter BIO.
        b64 = BIO_new(BIO_f_base64());
        // Chain the Base64 BIO on top of the memory BIO.
        bio = BIO_push(b64, bio);

        // Optionally disable newlines.
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

        // Allocate a buffer for the decoded data.
        std::vector<unsigned char> decoded(encoded.size());
        int decodedLength = BIO_read(bio, decoded.data(), encoded.size());
        if (decodedLength <= 0) {
            BIO_free_all(bio);
            throw std::runtime_error("BIO_read failed during Base64 decoding");
        }
        decoded.resize(decodedLength);

        BIO_free_all(bio);
        return decoded;
    }

    //-------------------------------------------------
    // Class for operations using the private key.
    // (Signing and Decryption)
    //-------------------------------------------------
    class Crypto {
      public:
        // Default constructor.
        Crypto() = default;

        // Constructor that loads a private key from a PEM string or file.
        // Pass true for isPEMString if pemData contains the PEM content.
        Crypto(const std::string &pemData, bool isPEMString = false) {
            if (isPEMString) {
                BIO *bio = BIO_new_mem_buf(pemData.data(), static_cast<int>(pemData.size()));
                if (!bio) {
                    throw std::runtime_error("Unable to create BIO from PEM string");
                }
                privateKey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
                BIO_free(bio);
                if (!privateKey) {
                    throw std::runtime_error("Unable to load private key from PEM string");
                }
            } else {
                FILE *keyFile = fopen(pemData.c_str(), "r");
                if (!keyFile) {
                    throw std::runtime_error("Unable to open private key file: " + pemData);
                }
                privateKey = PEM_read_PrivateKey(keyFile, nullptr, nullptr, nullptr);
                fclose(keyFile);
                if (!privateKey) {
                    throw std::runtime_error("Unable to read private key from file: " + pemData);
                }
            }
        }

        ~Crypto() {
            if (privateKey) {
                EVP_PKEY_free(privateKey);
            }
        }

        // Signs data using SHA-256 and returns the signature as a vector of bytes.
        std::vector<unsigned char> sign(const std::string &data) {
            EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
            if (!mdctx) {
                throw std::runtime_error("Unable to create EVP_MD_CTX for signing");
            }
            if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, privateKey) != 1) {
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

        // Decrypts ciphertext using the private key and returns the plaintext bytes.
        std::vector<unsigned char> decrypt(const std::vector<unsigned char> &ciphertext) {
            EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(privateKey, nullptr);
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

        // Convenience function: decrypt ciphertext and return the result as a string.
        std::string decryptToString(const std::vector<unsigned char> &ciphertext) {
            std::vector<unsigned char> plaintext = decrypt(ciphertext);
            return std::string(plaintext.begin(), plaintext.end());
        }

        // Returns the public key (PEM formatted) extracted from the private key.
        std::string getPublicHalf() {
            BIO *bio = BIO_new(BIO_s_mem());
            if (!bio) {
                throw std::runtime_error("Unable to create BIO for public key extraction");
            }
            if (!PEM_write_bio_PUBKEY(bio, privateKey)) {
                BIO_free(bio);
                throw std::runtime_error("Failed to write public key to BIO");
            }
            BUF_MEM *bptr = nullptr;
            BIO_get_mem_ptr(bio, &bptr);
            std::string publicKey(bptr->data, bptr->length);
            BIO_free(bio);
            return publicKey;
        }

      private:
        EVP_PKEY *privateKey = nullptr;
    };

    //-------------------------------------------------
    // Helper function to load a public key from a PEM string.
    //-------------------------------------------------
    inline EVP_PKEY *loadPublicKeyFromPEM(const std::string &pemPublic) {
        BIO *bio = BIO_new_mem_buf(pemPublic.data(), static_cast<int>(pemPublic.size()));
        if (!bio) {
            throw std::runtime_error("Unable to create BIO from public PEM string");
        }
        EVP_PKEY *pubkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pubkey) {
            throw std::runtime_error("Unable to load public key from PEM string");
        }
        return pubkey;
    }

    //-------------------------------------------------
    // Standalone functions for public key operations.
    // (Verification and Encryption)
    //-------------------------------------------------

    // Verifies that the signature matches the given data using the provided public key PEM string.
    inline bool verify(const std::string &pemPublic, const std::string &data,
                       const std::vector<unsigned char> &signature) {
        EVP_PKEY *pubkey = loadPublicKeyFromPEM(pemPublic);
        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("Unable to create EVP_MD_CTX for verification");
        }
        if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubkey) != 1) {
            EVP_MD_CTX_free(mdctx);
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("EVP_DigestVerifyInit failed");
        }
        if (EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("EVP_DigestVerifyUpdate failed");
        }
        int ret = EVP_DigestVerifyFinal(mdctx, signature.data(), signature.size());
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pubkey);
        return (ret == 1);
    }

    inline bool verify(EVP_PKEY *pubkey, const std::string &data, const std::vector<unsigned char> &signature) {
        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            throw std::runtime_error("Unable to create EVP_MD_CTX for verification");
        }
        if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubkey) != 1) {
            EVP_MD_CTX_free(mdctx);
            throw std::runtime_error("EVP_DigestVerifyInit failed");
        }
        if (EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            throw std::runtime_error("EVP_DigestVerifyUpdate failed");
        }
        int ret = EVP_DigestVerifyFinal(mdctx, signature.data(), signature.size());
        EVP_MD_CTX_free(mdctx);
        // Do not free(pubkey) here if the caller is managing its lifetime.
        return (ret == 1);
    }

    // Encrypts plaintext (as bytes) using the provided public key PEM string and returns ciphertext.
    inline std::vector<unsigned char> encrypt(const std::string &pemPublic,
                                              const std::vector<unsigned char> &plaintext) {
        EVP_PKEY *pubkey = loadPublicKeyFromPEM(pemPublic);
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pubkey, nullptr);
        if (!ctx) {
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("Unable to create EVP_PKEY_CTX for encryption");
        }
        if (EVP_PKEY_encrypt_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("EVP_PKEY_encrypt_init failed");
        }
        size_t outlen = 0;
        if (EVP_PKEY_encrypt(ctx, nullptr, &outlen, plaintext.data(), plaintext.size()) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("EVP_PKEY_encrypt (get length) failed");
        }
        std::vector<unsigned char> ciphertext(outlen);
        if (EVP_PKEY_encrypt(ctx, ciphertext.data(), &outlen, plaintext.data(), plaintext.size()) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            EVP_PKEY_free(pubkey);
            throw std::runtime_error("EVP_PKEY_encrypt failed");
        }
        ciphertext.resize(outlen);
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pubkey);
        return ciphertext;
    }

    inline std::vector<unsigned char> encrypt(EVP_PKEY *pubkey, const std::string &plaintextStr) {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pubkey, nullptr);
        if (!ctx) {
            throw std::runtime_error("Unable to create EVP_PKEY_CTX for encryption");
        }
        if (EVP_PKEY_encrypt_init(ctx) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            throw std::runtime_error("EVP_PKEY_encrypt_init failed");
        }
        std::vector<unsigned char> plaintext(plaintextStr.begin(), plaintextStr.end());
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

    // Overload: Encrypts a string plaintext using the provided public key PEM string.
    inline std::vector<unsigned char> encrypt(const std::string &pemPublic, const std::string &plaintextStr) {
        std::vector<unsigned char> plaintext(plaintextStr.begin(), plaintextStr.end());
        return encrypt(pemPublic, plaintext);
    }

} // namespace chain
