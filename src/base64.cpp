#include "../include/base64.h"
#include <iostream>

const std::string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

bool is_base64(unsigned char c)
{
    return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(unsigned const char* byteToEncode, unsigned int byteLength)
{
    std::string ret; //返回的是一种字符串类型 
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    while(byteLength--)
    {
        char_array_3[i++] = *(byteToEncode++);
        if(i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = (char_array_3[2] & 0x3f);

            for(int j = 0; j < 4; j++){
                ret += base64_chars[char_array_4[j]];
            }
            // std::cout << ret << std::endl;
            i = 0;
        }
        // std::cout << char_array_3[0] << char_array_3[1] << char_array_3[2] << std::endl;
    }
    if(i)
    {
        for(int j = 1; j < 3; j++){
            char_array_3[j] = '\0';
        }
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = (char_array_3[2] & 0x3f);
        for(int j = 0; j < 4; j++){
            if(j < (i + 1))
            {
                ret += base64_chars[char_array_4[j]];
            }
            else
            {
                ret += '=';
            }
        }
    }
    return ret;

}


std::string base64_decode(std::string const &encodeString) //解码 
{
    int len = encodeString.length();
    std::string ret;
    unsigned char char_array_4[4];
    unsigned char char_array_3[3];
    int i = 0;
    int in = 0;
    while(len-- && encodeString[in] != '=' && is_base64(encodeString[in]))
    {
        char_array_4[i++] = encodeString[in];
        in++;
        if ( i == 4)
        {
            for (i = 0; i < 4; i++) 
	        {	
	            char_array_4[i] = base64_chars.find(char_array_4[i]);  // 字符转换为索引下标，方便后续的计算0-64
	        }
            // 加入的原因是在编码时生成的A，并不是ACSII码值65，而是由0得来的，所以需要根据base64_cahrs得到对应的“原值”
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

            for(int j = 0;j < 3; j++)
            {
                ret += char_array_3[j];
            }
            std::cout << ret << std::endl;
            i = 0;
        }
        
       
    }
    std::cout << i << std::endl;
    if(i)
    {
        for(int j = i; j < 4; j++)
        {
            char_array_4[j] = 0;
        }
        for (int j = 0; j < 4; j++) 
	    {	
	        char_array_4[j] = base64_chars.find(char_array_4[j]);
	    }
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];
        std::cout << char_array_3[0] << char_array_3[1] << char_array_3[2] << std::endl;
        std::cout << ret << std::endl;
        for(int j = 0; j < i ; j++)
        {
            ret += char_array_3[j];
        }
        std::cout << ret << std::endl;  // NULL（空字符）	0	0x00
    }
    return ret;
}
