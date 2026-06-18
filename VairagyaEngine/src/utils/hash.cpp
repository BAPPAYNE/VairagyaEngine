#include "utils/hash.hpp"

#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <string>

using namespace std;

// Generate SHA256 hash value from input string
const basic_string<char> sha256(const string& input) {
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
	stringstream ss;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
		ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
	}
	return ss.str();
}