#ifndef __HTTP_POST_H__
#define __HTTP_POST_H__

#include "json.h"

typedef struct
{
	unsigned int     port;
	std::string  path;
	std::string  host;
	std::string  val;
	std::string  schema;
}Http_Post_ST;

std::string JsonToString2(const Json::Value & root);

class HttpHelper {
public:
    int Http_Post_Server_Add(std::string url ,std::string token);

    int Http_Post_Message(Json::Value& message);
    int Http_Post_Message(Json::Value& message, int index);
    int Http_Post_Message(Json::Value& message, int index, std::string& key);
    int Http_Post_Message(Json::Value& message, int index, Json::Value& json);

private:
    std::set<std::string> gHttpPostConfig_Set;
    std::vector<Http_Post_ST> gHttpPostConfig_List;

    int gTotalNum = 0;
    int mMaxRetry = 0;

private:
    int http_Post_Message(Json::Value& message);
    int http_Post_Message(Json::Value& message, int index);
    int http_Post_Message(Json::Value& message, int index, std::string& key);
    int http_Post_Message(Json::Value& message, int index, Json::Value& json);
};

#endif
