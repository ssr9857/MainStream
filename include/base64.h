#ifndef BASE64_H_
#define BASE64_H_

#include <iostream>
#include <vector>
#include <string>

std::string base64_encode(unsigned const char* byteToEncode, unsigned int len);

std::string base64_decode(std::string const &encodeString);

#endif
