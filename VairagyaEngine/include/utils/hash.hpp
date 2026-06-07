#ifndef HASH_H
#define HASH_H

#include <string>
#include <cctype>

using namespace std;

const basic_string<char> sha256(const string& input); // compute sha256 hash of the input

#endif // HASH_H