

#include <iostream>
#include <chrono>
#include <mutex>
#include <regex>
#include <condition_variable>
// #include "CGraph.h"

#include "glog/logging.h"

#include "minio-cpp/minio_client.hpp"
#include "minio-cpp/ilogger.hpp"
#include "rknn/rk3588_node.h"

#include "./rk3588/include/WKTParser.h"
#include "./rk3588/include/AINode.h"
#include "./rk3588/include/znkj_nvr.h"
#include "./rk3588/include/base64.h"

using namespace CGraph;
using namespace chrono;

std::shared_ptr<SeawayEdgeInterface> p_seawayedge_interface = std::make_shared<SeawayEdgeInterface>(); // seawaySDK实例

std::mutex exit_mutex;
std::condition_variable exit_cv; // 线程间同步

bool exit_flag = false; // 退出标志

// 功能：初始化
class InitNode : public CGraph::GNode 
{
	public:
		CStatus init () override 
		{
			CStatus status;
			CGraph::CGRAPH_ECHO("InitNode init.");
			return status;
		}

		CStatus run () override 
		{
			CStatus status;
			CGraph::CGRAPH_ECHO("InitNode run.");
			// int id  = EdgeData.camera_id; // 这种方式有可能得到的是旧值
			// 创建参数信息 参数列表：(Type, key) 
			CGRAPH_CREATE_GPARAM(sendInfoParam, "send-param");  // 为什么只有初始化和获取，没有发送
			// 获取在ReadNode中，获取的是send-recv主题接受的对象。
			return status;
		}
	// private:
	// 	EdgeInterfaceDate EdgeData = p_seawayedge_interface->GetEdgeIDate();
};

// 功能:从指定的消息主题中接收数据，并将其存储到中
class ReadNode : public CGraph::GNode 
{
	public:
		CStatus run() override 
		{
			// CGraph::CGRAPH_ECHO("ReadNode run");
			std::unique_ptr<pipelineInfoMessageParam> tempdata = nullptr;
			// 有等待时间的接收一个 pipelineInfoMessageParam 类型的消息  参数列表：(Type, topic, value, timeout) 
			// 该消息的发送在rk3588Node的run函数中
			CStatus status = CGRAPH_RECV_MPARAM_WITH_TIMEOUT(pipelineInfoMessageParam, "send-recv", tempdata, 1*10); // 问题：这里的"；"是否删去  发送在rk3588Node中

			if (!status.isOK()) 
			{
				run_loop = true;
				CGraph::CGRAPH_ECHO("ReadNode recv message error");
				std::lock_guard<std::mutex> lock(exit_mutex);
				if(!exit_flag)
					return CStatus();
				else
					return status;
			}

			run_loop = false;
			// 获取参数信息，为空则抛出异常  参数列表：(Type, key)
			// 把接收到的 pipelineinfo 添加到 sendInfoParam 的 pipelineinfo_list 中，列表大小达到阈值 PIPELINEINFO_LIST_THRESH，就会输出日志信息
			auto *pipelineparam = CGRAPH_GET_GPARAM_WITH_NO_EMPTY(sendInfoParam, "send-param") 
			{
				// 上参数写锁 参数列表：(param) 
				CGRAPH_PARAM_WRITE_CODE_BLOCK(pipelineparam)
				{
					pipelineparam->pipelineinfo_list.push_back(tempdata->pipelineinfo); // 不断将pipelineInfoMessageParam加入自己的topic然后进行处理
				}
				if(pipelineparam->pipelineinfo_list.size() >= PIPELINEINFO_LIST_THRESH) // piplineInfo_list的阈值为120
					LOG(INFO) << "SendInfoParam out of thresh";
			}
	
			return status;
		}
		// 用于判定该节点是否需要暂停或者等待
		CBool isHold() override 
		{
			return run_loop;
		}

	private:
		bool run_loop = false;
};

// 功能：对管道信息中的告警对象进行筛选，移除不在指定多边形 ROI 内的对象，isInPolygon 方法用于检查矩形框是否在多边形内
class AppRoiNode : public CGraph::GNode
{
	public:
		// 初始化
		CStatus init() override
		{
			return CStatus();  //如果是CStatus status  则return status;  否则：return CStatus();
		}
		// 运行
		CStatus run() override
		{
			pipelineInfo pipelineinfo;
			// 获取参数信息，为空则抛出异常 参数列表：(Type, key) 
			auto *sendinfoparam = CGRAPH_GET_GPARAM_WITH_NO_EMPTY(sendInfoParam, "send_param")  // 对上边处理完的管道信息继续处理
			{
				CGRAPH_PARAM_WRITE_CODE_BLOCK(sendinfoparam) // 后面跟着上锁的范围
				{
					pipelineinfo = sendinfoparam->pipelineinfo_list.front();
					std::list<objectInfo>::iterator alarm_object_iterator = pipelineinfo.alarm_information.alarm_object_list.begin();
					while(alarm_object_iterator != pipelineinfo.alarm_information.alarm_object_list.end())
					{
						auto alarm_object = *alarm_object_iterator;
						// 创建一个 cv::Rect 类型的对象 tempObjectRect，该对象代表一个矩形区域.
						cv::Rect tempObjectRect = cv::Rect(alarm_object.x,
															alarm_object.y,
															alarm_object.w,
															alarm_object.h);
						// 参数列表：（可画框区域，原图区域，目标框区域）检测目标框是否在可画框区域内
						if(!isInPolygon(pipelineinfo.setting_information.roi, cv::Size(pipelineinfo.source_image.cols, pipelineinfo.source_image.rows), tempObjectRect))
						{
							alarm_object_iterator = pipelineinfo.alarm_information.alarm_object_list.erase(alarm_object_iterator);
							sendinfoparam->pipelineinfo_list.front().alarm_information.alarm_num--;
						}
						else
						{
							++alarm_object_iterator;
						}
					}
				}
			}
			return CStatus();
		}
		// 检查
		bool isInPolygon(std::string roi_polygon, cv::Size img_size, cv::Rect obj_rect)
		{
			WKTParser WKT_Handle(img_size);
			VectorPoint wkt_points;
			WKT_Handle.parsePolygon(roi_polygon, &wkt_points);
			return WKT_Handle.inPolygon(wkt_points, obj_rect);
		}
};

// 功能：接收和处理 MQTT 消息，进行图像绘制和数据处理，以及将处理后的数据发送到不同的目标（如 MQTT、Minio 和 Kafka）
class AppMqttNode : public CGraph::GNode
{
	public:
		// 功能：初始化
		CStatus init() override
		{
			CGraph::CGRAPH_ECHO("AppMqttNode init");

			// 初始化minio客户端智能的指针
			if(!p_seawayedge_interface->GetEdgeIDate().global_minio_server.empty())
			{
				p_minio = std::make_unique<MinioClient>(p_seawayedge_interface->GetEdgeIDate().global_minio_server, p_seawayedge_interface->GetEdgeIDate().global_minio_access_key, p_seawayedge_interface->GetEdgeIDate().global_minio_secret_key);
			}
			// 初始化网络视频摄像机客户端的指针
			if(p_seawayedge_interface->GetEdgeIDate().global_nvr_enable)
			{
				p_znkj_nvr_client = std::make_unique<ZnkjNvrClient>(p_seawayedge_interface->GetEdgeIDate().global_nvr_ip, p_seawayedge_interface->GetEdgeIDate().global_nvr_port);
			}

			return CStatus();
		}
		// 功能：运行
		CStatus run() override
		{
			pipelineInfo mqttinfo;
			Json::Value root; // 用来存储读取到的json中参数值  问题：root中保存的信息发给谁？
			std::unique_ptr<mqttMessageParam> tempdata = nullptr;

			// 有等待时间的接收一个 message param 参数列表：(Type, topic, value, timeout)
			// 获取的消息是pipelineinfo类型，赋值给tempdata
			CStatus status = CGRAPH_RECV_MPARAM_WITH_TIMEOUT(mqttMessageParam, "mqtt-param", tempdata, 1*10); // 单位为ms  接受mqtt-param
			if (!status.isOK()) {
				CGraph::CGRAPH_ECHO("AppMqttNode recv message error");
				return CStatus();  // 问题：return status； 和return CStatus();的区别
			}

			mqttinfo = tempdata->pipelineinfo;
			std::cout << "mqttinfo.alarm_date = " << mqttinfo.alarm_date << std::endl; 
			//将报警时间alarm_date的字符串内容格式YYYY-MM-DDTHH:MM:SS.mmm+HH:MM转换为YYYYMMDDHHMMSSmmm
			std::string alarm_date_str = timezone_and_format_time_with_offset_and_convert(mqttinfo.alarm_date, 0); 

			// 开启网络视频摄像机
			if(p_seawayedge_interface->GetEdgeIDate().global_nvr_enable)
			{
				int channel = 0;
				if(nvr_record_event(mqttinfo.camera_index, channel, alarm_date_str))
				{
					// 读取edge全局信息到root中，用于后续的数据传输
					auto time_stamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					root["information"]["nvr_ip"] = p_seawayedge_interface->GetEdgeIDate().global_nvr_ip;
					root["information"]["nvr_port"] = p_seawayedge_interface->GetEdgeIDate().global_nvr_port;
					root["information"]["nvr_user"] = p_seawayedge_interface->GetEdgeIDate().global_nvr_user;
					root["information"]["nvr_password"] = p_seawayedge_interface->GetEdgeIDate().global_nvr_password;
					root["information"]["nvr_channel"] = channel;
					root["information"]["nvr_timestamp"] = time_stamp_ms;
					root["channel_no"] = channel;
					root["video_url"] = "http://" + p_seawayedge_interface->GetEdgeIDate().global_minio_server + "/nvralarm/" + alarm_date_str + ".mp4";
					// YYYYMMDDHHMMSSmmm转换为YYYY-MM-DDTHH:MM:SS.mmm+HH:MM
					root["stop_alarm_date"] = format_time_with_offset_and_convert(alarm_date_str, p_seawayedge_interface->GetEdgeIDate().global_nvr_duration); 
					std::cout << "channel_no = " << root["channel_no"] << std::endl;
					std::cout << "video_url = " << root["video_url"] << std::endl;
					std::cout << "stop_alarm_date = " << root["stop_alarm_date"] << std::endl;
				}
			}

			// 在原图画框和添加描述
			cv::Mat bgr_frame = mqttinfo.source_image.clone();
			for(auto alarm_array_info : mqttinfo.result_information.result_object_list) // alarm_array_info是objectInfo结构体列表
			{	
				// 参数列表： 【原图，矩阵框（左上角坐标，宽，高），颜色，边框粗细】
				cv::rectangle(bgr_frame, cv::Rect(alarm_array_info.x, alarm_array_info.y,
							alarm_array_info.w, alarm_array_info.h), cv::Scalar(0, 0, 255), 4); 
				// 参数列表：【原图，字符串，字符串位置，字体类型，字体大小，颜色，文本笔画粗细】
				cv::putText(bgr_frame, alarm_array_info.label + " " + formatFloatValue(alarm_array_info.score, 2), 
								cv::Point(alarm_array_info.x, alarm_array_info.y - 10), cv::FONT_HERSHEY_SIMPLEX, 3, cv::Scalar(0, 0, 255), 4);
			}

			// 对原图和压缩后的图进行编码用于mqtt发送
			cv::Mat dst_thumbnail_mat; 
			cv::resize(bgr_frame, dst_thumbnail_mat, cv::Size(mqttinfo.source_image.cols / 4, mqttinfo.source_image.rows / 4), 0, 0, cv::INTER_NEAREST);
			std::string full_str = Mat2Base64(bgr_frame, ".jpg"); // 原图编码
			std::string thumbnail_str = Mat2Base64(dst_thumbnail_mat, ".jpg");  // 缩小四倍后编码

			// 读取pipelineInfo中信息 // root几乎把piplineinfo中所有信息全部复制了，另外存储了一些其它信息，eg:alarm_raw_pic,alarm_thumbnail_pic
			root["camera_id"]      = stringToInt64(mqttinfo.camera_id);
			root["camera_name"]    = mqttinfo.camera_name;
			root["deployment"] = mqttinfo.seawayos_app;
			root["namespace"] = mqttinfo.seawayos_namespace;
			root["alarm_type"] = mqttinfo.alarm_type;
			root["alarm_raw_pic"]       = full_str;
			root["alarm_thumbnail_pic"] = thumbnail_str;
			root["alarm_date"] = mqttinfo.alarm_date; // 报警时间
			root["information"]["alarm_num"] = mqttinfo.alarm_information.alarm_num;
			root["information"]["result_num"] = mqttinfo.result_information.result_num;

			// 读取该帧(管道)的变量值到root的中，用于mqtt消息发送
			Json::Value temp_alarm_array, alarm_array; 
			Json::Value temp_result_array, result_array;// 用来保存值的
			for(auto alarm_array_info : mqttinfo.alarm_information.alarm_object_list)
			{
				temp_alarm_array["x"] = alarm_array_info.x;
				temp_alarm_array["y"] = alarm_array_info.y;
				temp_alarm_array["w"] = alarm_array_info.w;
				temp_alarm_array["h"] = alarm_array_info.h;
				temp_alarm_array["track_id"] = alarm_array_info.track_id;
				temp_alarm_array["label"] = alarm_array_info.label;
				LOG(INFO) << " camera_index = " << mqttinfo.camera_index << std::endl; 
				LOG(INFO) << "alarm_array_info.track_id = " << alarm_array_info.track_id << " label = " << alarm_array_info.label << " score = " << alarm_array_info.score << std::endl;
				temp_alarm_array["score"] = alarm_array_info.score;
				alarm_array.append(temp_alarm_array);
			}
			root["information"]["alarm"] = alarm_array; // 没有result_information了，直接将其成员作为information下的
			for(auto result_array_info : mqttinfo.result_information.result_object_list)
			{
				temp_result_array["x"] = result_array_info.x;
				temp_result_array["y"] = result_array_info.y;
				temp_result_array["w"] = result_array_info.w;
				temp_result_array["h"] = result_array_info.h;
				temp_result_array["track_id"] = result_array_info.track_id;
				temp_result_array["label"] = result_array_info.label;
				temp_result_array["score"] = formatFloatValue(result_array_info.score, 1);
				result_array.append(temp_result_array);
			}
			root["information"]["result"] = result_array;
			// 读取管道中的setting_information(settingInfo结构体)到mqtt消息中
			root["information"]["settings"]["roi"] = mqttinfo.setting_information.roi;
			root["information"]["settings"]["FrameInterval"] = mqttinfo.setting_information.frameInterval;
			root["information"]["settings"]["NmsThreshold"] = formatFloatValue(mqttinfo.setting_information.nmsThreshold, 1);
			root["information"]["settings"]["AlarmInterval"] = mqttinfo.setting_information.alarmInterval;
			root["information"]["settings"]["AlarmSmooth"] = mqttinfo.setting_information.alarmSmooth;
			root["information"]["settings"]["StatisticsStartTime"] = mqttinfo.setting_information.statisiticsStartTime;
			root["information"]["settings"]["StatisticsEndTime"] = mqttinfo.setting_information.statisiticsEndTime;
			for(auto temp_thresh_map : mqttinfo.setting_information.labelThreshMap)
			{
				// std::map<std::string ,float>   first表示键， seconde表示值（保留一位小数）
				root["information"]["settings"][temp_thresh_map.first] = formatFloatValue(temp_thresh_map.second, 1);
			}

			std::string message = Json2String(root); // root中的JSON格式消息转换为字符串
			if(p_seawayedge_interface->SendEdgeIMqttMessage(message)) //消息进行发布，发布到mqtt服务器，等待被订阅
				// 并没有使用CGraph的GMessageParam,直接发送mqt消息，我认为这里的mqtt和kafka发送的消息用于传给seawayedge平台渲染用的
				LOG(INFO) << "update alarm info success" << std::endl;
			else
				LOG(INFO) << "update alarm info fail" << std::endl;


			// 将图像进行编码，类型为jpg格式，然后保存到minio服务器中，minio服务器用于保存报警图片文件（mp4）
			std::string alarm_image_path;
			auto time_stamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if(!p_seawayedge_interface->GetEdgeIDate().global_minio_server.empty())
			{
#if 0
				// save image for minio
				std::vector<uchar> buf;
				std::vector<int> compressionParams;
				compressionParams.push_back(cv::IMWRITE_JPEG_QUALITY);
				compressionParams.push_back(50);
				cv::imencode(".jpg", bgr_frame, buf, compressionParams);
				alarm_image_path = "/algo-alarm/" + std::to_string(time_stamp_ms) + ".jpg";
				auto buckets = p_minio->get_bucket_list();
				if(!buckets.empty())
				{
					if(p_minio->upload_filedata(alarm_image_path, buf.data(), buf.size())){
						INFO("upload %s success, size: %d bytes", alarm_image_path.c_str(), buf.size());
					}else
					{
						INFO("WARNING: upload %s fail, size: %d bytes", alarm_image_path.c_str(), buf.size());
					}
				}
				else
				{
					INFO("WARNING: no correct bucket");
					// return CStatus();
				}
#endif
			} // minio服务器

			// 保存kafka的JSON格式消息，进行发布，等待被订阅
			if(!p_seawayedge_interface->GetEdgeIDate().global_kafka_broker.empty())
			{
				auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				Json::Value kafka_root;
				kafka_root["alertName"] = "发现wz人员";
				kafka_root["data"]["alarmDate"] = timestamp;
				kafka_root["data"]["altitude"] = "1.0";
				kafka_root["data"]["picUrl"] = alarm_image_path;
				kafka_root["data"]["channelName"] = "5号jb-红外-新建";
				kafka_root["data"]["deviceId"] = "99163901901329000007";
				kafka_root["data"]["deviceName"] = "5号jb";
				kafka_root["data"]["eventId"] = 12374871;
				kafka_root["data"]["isRoot"] = 1;
				kafka_root["data"]["latitude"] = "39.913356";
				kafka_root["data"]["liveVideo"]["type"] = "GB";
				kafka_root["data"]["liveVideo"]["value"] = "34020000001320074885";
				kafka_root["data"]["longtitude"] = "124.221961";
				kafka_root["data"]["objType"] = "30002";
				kafka_root["data"]["recordVideo"]["type"] = "GB";
				kafka_root["data"]["recordVideo"]["value"] = "23475613678313545";
				kafka_root["data"]["source"] = "边缘计算";

				kafka_root["deviceId"] = kafka_root["data"]["deviceId"];
				kafka_root["event"] = "specialEvent";
				kafka_root["headers"]["deviceName"] = kafka_root["data"]["deviceName"];
				kafka_root["headers"]["orgId"] = "";
				kafka_root["headers"]["productId"] = "JKSB-SXT";
				kafka_root["headers"]["type"] = "300002";

				kafka_root["messageId"] = "45453271231234524564";
				kafka_root["messageType"] = "EVENT";
				kafka_root["timestamp"] = time_stamp_ms;

				std::string kafka_message = Json2String(kafka_root);
				p_seawayedge_interface->SendEdgeIKafkaMessage(kafka_message, "0"); // 发布kafka消息，并没有使用CGraph的GMessageParam
			}
			return CStatus();
		} // run()

		CBool isHold() override 
		{
			std::lock_guard<std::mutex> lock(exit_mutex);
			return !exit_flag;
		}


	private:
		// EdgeInterfaceDate EdgeData = p_seawayedge_interface->GetEdgeIDate();  不可取，有可能读取的是旧值
		std::unique_ptr<MinioClient> p_minio;
		bool state_nvr_login = false;
    	std::unique_ptr<ZnkjNvrClient> p_znkj_nvr_client;

		// 功能：从输入的 URL 中提取通道号，并尝试启动录像record_nvr
		bool nvr_record_event(int camera_index, int &channel, std::string alarm_date_str)
		{
			std::regex re("(\\d+)$"); // 其作用是匹配字符串末尾的一个或多个数字
			std::smatch match; // 用于存放正则表达式匹配的结果
			std::string str = p_seawayedge_interface->GetEdgeIDate().camera_url[camera_index]; // rtsp://admin:admin@192.168.1.169:554/mainstram
			
			int duration = p_seawayedge_interface->GetEdgeIDate().global_nvr_duration;
			int lastDigit = 0;

			// 对输入的 URL 执行正则表达式匹配
			if (std::regex_search(str, match, re)) 
			{
				lastDigit = std::stoi(match.str(0)); // 用于将字符串转换为 int 类型的整数，match.str(0)返回完整匹配的结果
				channel = lastDigit;
				LOG(INFO) << "record channel is: " << lastDigit << std::endl;
			} 
			else 
			{
				LOG(INFO) << "No digits found at the end of the string." << std::endl;
			}

			std::string nvr_ip = p_seawayedge_interface->GetEdgeIDate().global_nvr_ip;
			int nvr_port = p_seawayedge_interface->GetEdgeIDate().global_nvr_port;
			// 进行nvr设备的启动
			if(lastDigit != 0 && p_znkj_nvr_client->record_nvr(nvr_ip, nvr_port, channel, duration, alarm_date_str))
			{
				LOG(INFO) << "record alarm success" << std::endl;
				return true;
			}
			else
			{
				LOG(INFO) << "record alarm fail" << std::endl;
				return false;
			}
			
		} // 函数nvr_record_evnet()

		// 功能：对浮点数位数进行限制
		std::string formatFloatValue(float val, int fixed) {
			std::ostringstream oss;
			oss << std::setprecision(fixed) << val;
			return oss.str();
		}
		
		// 功能：将json文件内容转换为字符串形式
		std::string Json2String(const Json::Value & root)
		{
			static Json::Value def = []()
			{
				Json::Value def;
				Json::StreamWriterBuilder::setDefaults(&def);
				def["emitUTF8"] = true;
				return def;
			}
			();
			std::ostringstream stream;
			Json::StreamWriterBuilder stream_builder;
			stream_builder.settings_ = def;
			std::unique_ptr<Json::StreamWriter> writer(stream_builder.newStreamWriter());
			writer->write(root, &stream);
			return stream.str();
		}

		// 功能：将 OpenCV 的 cv::Mat 图像对象先压缩为JPEG格式，然后转换为 Base64 编码的字符串
		std::string Mat2Base64(const cv::Mat image, std::string imgType)
		{
			std::vector<uchar> buf; // uchar是opencv中的类型
			std::vector<int> compressionParams;
			// 将图像编码类型设置为 JPEG 质量参数
			compressionParams.push_back(cv::IMWRITE_JPEG_QUALITY); // 这是opencv4版本的，CV_IMWRITE_JPEG_QUALITY是opencv2
			// JPEG是24位的格式，高效的压缩格式
			compressionParams.push_back(50);

			cv::imencode(imgType, image, buf, compressionParams); // 将图像数据编码为指定格式的内存缓冲区，常用于图像压缩和网络传输等场景
			// buf.data()会返回一个指向容器中第一个元素的指针。
			std::string img_data = base64_encode(buf.data(), buf.size()); // 编码后的字符只有A-Z a-z 0-9 / +

			return img_data;
		}

		// 功能：将字符串转换为 int64_t 类型的整数
		int64_t stringToInt64(const std::string& str) 
		{
			try {
				return std::stoll(str); // 将字符串 str 转换为 int64_t 类型
			} 
			catch (const std::invalid_argument& e) 
			{
				std::cerr << "Invalid argument: " << e.what() << std::endl;
				
				return 0;  
			} catch (const std::out_of_range& e) 
			{
				std::cerr << "Out of range: " << e.what() << std::endl;
				
				return 0;  
			}
		}

		// 流程： 1.读取YYYYMMDDHHMMSS保存为tm结构体类型 2.读取mmm保存为整数类型
			//    3.
		// 功能：对输入的时间字符串（格式为 YYYYMMDDHHMMSSmmm）应用指定的偏移秒数，并将结果格式化为 YYYY-MM-DDTHH:MM:SS.mmm+08:00 格式
		std::string format_time_with_offset_and_convert(const std::string& time_str, int offset_seconds)
		{
			// 输入格式：YYYYMMDDHHMMSSmmm
			if (time_str.length() != 17) 
			{
				throw std::runtime_error("Invalid time string length");
			}

			// 解析时间字符串（YYYYMMDDHHMMSS格式）
			std::tm tm = {};
			std::istringstream ss_date(time_str.substr(0, 14)); // 提取YYYYMMDDHHMMSS部分
			ss_date >> std::get_time(&tm, "%Y%m%d%H%M%S"); // 将ss_date【std::istringstream类型】的数据解析到tm结构体中
			if (ss_date.fail()) 
			{
				throw std::runtime_error("Failed to parse time string");
			}

			// 提取毫秒部分
			int milli = std::stoi(time_str.substr(14, 3)); // 提取mmm部分
			// 把 std::tm 结构体表示的本地时间转换为自纪元以来的秒数 time_t 类型 便于后续计算，offset
			time_t time_in_seconds = std::mktime(&tm);
			if (time_in_seconds == -1) 
			{
				throw std::runtime_error("Failed to convert to time_t");
			}
			// 应用偏移秒数
			time_in_seconds += offset_seconds;
			// 将offset后的时间转换为本地时间（北京时间），并存储为tm结构体中
			std::tm* local_tm = std::localtime(&time_in_seconds);
			
			// 使用 stringstream 来格式化结果字符串
			std::stringstream result_ss;
			result_ss << std::put_time(local_tm, "%Y-%m-%dT%H:%M:%S"); // 将 std::tm 结构体表示的时间按照指定的格式输出到流中
			result_ss << '.' << std::setw(3) << std::setfill('0') << milli; // 保留毫秒部分
			result_ss << "+08:00"; // 确保是北京时间
			return result_ss.str();
		}

		// 功能：解析输入的时间字符串（格式为 YYYY-MM-DDTHH:MM:SS.mmm+HH:MM），应用指定的偏移秒数，并将结果格式化为 YYYYMMDDHHMMSSmmm 格式
		std::string timezone_and_format_time_with_offset_and_convert(const std::string& time_str, int offset_seconds)
		{
			std::tm tm = {};
			int milli = 0;
			char tz_sign = '+';
			int tz_hour = 0, tz_min = 0;
			// 使用 istringstream 来解析时间字符串
			std::istringstream ss(time_str);
			// 解析年-月-日T时:分:秒
			ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
			if (ss.fail()) {
				throw std::runtime_error("Failed to parse date and time");
			}
			// 解析毫秒部分
			ss.ignore(1, '.'); // 忽略点号
			ss >> milli;
			// 解析时区部分
			ss >> tz_sign >> std::setw(2) >> tz_hour;
			ss.ignore(1, ':'); // 忽略冒号
			ss >> std::setw(2) >> tz_min;
			// 计算时区偏移量（转换为秒）
			int tz_offset_seconds = (tz_hour * 3600 + tz_min * 60) * (tz_sign == '+' ? 1 : -1);
			// 将时间转换为 time_t 类型以便于计算
			time_t time_in_seconds = std::mktime(&tm);
			if (time_in_seconds == -1) {
				throw std::runtime_error("Failed to convert to time_t");
			}
			// 将时间调整为本地时间，应用时区偏移量
			// time_in_seconds += tz_offset_seconds;
			// 应用额外的偏移秒数
			time_in_seconds += offset_seconds;
			// 将调整后的时间转换回 tm 结构
			tm = *std::localtime(&time_in_seconds); // 使用 localtime 而不是 gmtime 来保持本地时区
			// 使用 stringstream 来格式化结果字符串
			std::stringstream result_ss;
			result_ss << std::put_time(&tm, "%Y%m%d%H%M%S"); // 按所需格式输出年月日时分秒
			result_ss << std::setw(3) << std::setfill('0') << milli; // 保留毫秒部分
			return result_ss.str();
		}
};

// 功能：结束节点
class AppEndNode : public CGraph::GNode 
{
	public:
		CStatus init() override {
			CGraph::CGRAPH_ECHO("AppTestNode init");
			return CStatus();
		}
	
		CStatus run() override {
			CGraph::CGRAPH_ECHO("AppEndNode run");
			pipelineInfo pipelineinfo;
			std::unique_ptr<mqttMessageParam> tempdata(new mqttMessageParam());
			// 读取类型为sendInfoParam的参数
			auto *sendinfoparam = CGRAPH_GET_GPARAM_WITH_NO_EMPTY(sendInfoParam, "send-param") 
			{
				CGRAPH_PARAM_READ_CODE_BLOCK(sendinfoparam)
				{
					pipelineinfo = sendinfoparam->pipelineinfo_list.front();
				}
			}

			if(pipelineinfo.alarm_information.alarm_num > 0)
			{
				tempdata->pipelineinfo = pipelineinfo;
				// 发送一个 message param ,参数列表：(Type, topic, value, strategy) 
				CStatus status = CGRAPH_SEND_MPARAM(mqttMessageParam, "mqtt-param", tempdata, CGraph::GMessagePushStrategy::WAIT) // 发送mqtt-param
			}

			{
				CGRAPH_PARAM_WRITE_CODE_BLOCK(sendinfoparam)
				if(sendinfoparam->pipelineinfo_list.size() != 0)
					sendinfoparam->pipelineinfo_list.pop_front();
			}
			return CStatus();
		}
};

// 功能：SEIParam参数节点，存储了 SeawayEdgeInterface 类型的对象
class SEIParamNode : public CGraph::GNode 
{
	public:
		CStatus init() override 
		{
			CStatus status;
			CGraph::CGRAPH_ECHO("init EdgeIParamNdoe");
			// 创建参数信息 参数列表：(Type, key) 
			status = CGRAPH_CREATE_GPARAM(SEIParam, "sei_data") 
			return status;
		}
	
		CStatus run() override 
		{
			// 获取参数信息，为空则抛出异常，参数列表：(Type, key) 
			auto* seawayedge_interface_param = CGRAPH_GET_GPARAM_WITH_NO_EMPTY(SEIParam, "sei_data")   
	
			std::string seawayedge_interface_version;
			{
				CGRAPH_PARAM_WRITE_CODE_BLOCK(seawayedge_interface_param)
				seawayedge_interface_param->sei = *p_seawayedge_interface;
			}
			return CStatus();
		}
};


// 功能：发送消息
void send_message() 
{
    CStatus status;
    GElementPtr algo_pipeline_init_node, algo_pipeline_node = nullptr;
    GPipelinePtr pubPipeLine = GPipelineFactory::create(); 

    // 注册用于发送的pipeline
    status += pubPipeLine->registerGElement<SEIParamNode>(&algo_pipeline_init_node, {}, "InitNode");
    status += pubPipeLine->registerGElement<RK3588Node>(&algo_pipeline_node, {algo_pipeline_init_node}, "RK3588Node");

    pubPipeLine->process();
    {
        std::lock_guard<std::mutex> lock(exit_mutex);
        exit_flag = true;
    }
    exit_cv.notify_all();
    pubPipeLine->destroy();
    GPipelineFactory::remove(pubPipeLine);
}

// 功能：发送mqtt消息
void mqtt_message() {
    CStatus status;
    GElementPtr appMqttNode = nullptr;
    GPipelinePtr mqttPipeLine = GPipelineFactory::create();

    status += mqttPipeLine->registerGElement<AppMqttNode>(&appMqttNode, {}, "appMqttNode");
    mqttPipeLine->process();
    std::unique_lock<std::mutex> lock(exit_mutex);
    exit_cv.wait(lock, [] { return exit_flag; });

    mqttPipeLine->cancel();
    mqttPipeLine->destroy();

    GPipelineFactory::remove(mqttPipeLine);
}


void recv_message() {
    CStatus status;
    GElementPtr initNode, appReadNode = nullptr;
	GElementPtr appRoiNode, appEndNode = nullptr;
    GElementPtr appLogicRegion = nullptr;

    GPipelinePtr subPipeline = GPipelineFactory::create();
	
    appReadNode = subPipeline->createGNode<ReadNode>(GNodeInfo({}, "readNode", 1));

    appRoiNode = subPipeline->createGNode<AppRoiNode>(GNodeInfo({appReadNode}, "appRoiNode", 1));

    appEndNode = subPipeline->createGNode<AppEndNode>(GNodeInfo({appRoiNode}, "appEndNode", 1));

    appLogicRegion = subPipeline->createGGroup<GRegion>({appReadNode,appRoiNode, appEndNode});

    if (nullptr == appLogicRegion) {
        return;
    }

    // 注册用于 接收 的pipeline
    status += subPipeline->registerGElement<InitNode>(&initNode, {}, "initNode");
    status += subPipeline->registerGElement<GRegion>(&appLogicRegion, {initNode}, "appLogicRegion", -1);
    status = subPipeline->makeSerial();
    CGRAPH_ECHO("pipeline makeSerial status is [%d]", status.getCode());
    subPipeline->process();
    std::unique_lock<std::mutex> lock(exit_mutex);
    exit_cv.wait(lock, [] { return exit_flag; });

    subPipeline->cancel();
    subPipeline->destroy();

    GPipelineFactory::remove(subPipeline);
}

// 功能：
// rkyolov5.cpp主要集和了preprocess.cpp（前处理）和postprocess.cpp（后处理）和rknnPool.h（线程池）完成了模型的多线程推理，推理用到了官方库librknnrt.so
// rk3588_node.cpp则通过rtsp协议从指定的网络视频摄像机读取帧，通过rkyolov5.cpp进行推理，推理后的结果通过frame_concate.cpp（帧拼接）再回传给rtsp服务器。
// 理解：而该main函数，则是对推理后的结果进行处理，处理过程使用了DAG（有向无换图）框架CGraph.
int main(int argc, char *argv[]) {

    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    // google::SetLogDestination(google::GLOG_INFO, "log/seawaystream.log");
    LOG(INFO) << "seawaystream version 1.2.2" << std::endl;
    LOG_DATE_TIME; // seawaystream start 2025-03-25 &H:53:59
	LOG(INFO) << "seawayEdge开始初始化" << std::endl;
    p_seawayedge_interface->InitEdgeI(argv[1], argv[2]); // 该函数就包含了外设参数获取和全局参数获取

    if(p_seawayedge_interface->GetEdgeIDate().global_config_enable && !p_seawayedge_interface->GetEdgeIDate().global_kafka_broker.empty())
    {
		// broker topic 和partition个数
        p_seawayedge_interface->InitEdgeIkafka(p_seawayedge_interface->GetEdgeIDate().global_kafka_broker, 
                                                p_seawayedge_interface->GetEdgeIDate().global_kafka_topic, 0);
    }
	// 创建一个topic，也在算子中实现创建流程   三部曲：创建，发送，接受，
    CGRAPH_CREATE_MESSAGE_TOPIC(pipelineInfoMessageParam, "send-recv", 48)     // 发送在rk3588Node的run()中，接受在ReadNode中
	// 创建一个topic，也在算子中实现创建流程
    CGRAPH_CREATE_MESSAGE_TOPIC(mqttMessageParam, "mqtt-param", 48) // 发送在AppEndNode，接收在AppMqttNode中。

	// 管道信息的流程  



    std::thread sendThd = std::thread(send_message);

#ifdef ALGO_INFER_ENABLE
    std::thread recvThd = std::thread(recv_message);
    std::thread mqttThd = std::thread(mqtt_message);
#endif

    sendThd.join();
    LOG(INFO) << "send_message thread exit..." << std::endl;

#ifdef ALGO_INFER_ENABLE
    recvThd.join();
    LOG(INFO) << "recv_message thread exit..." << std::endl;
    mqttThd.join();
    LOG(INFO) << "mqtt_message thread exit..." << std::endl;
#endif

    CGRAPH_CLEAR_MESSAGES();
    GPipelineFactory::clear();
    LOG(INFO) << "all thread exit..." << std::endl;
    return 0;
}