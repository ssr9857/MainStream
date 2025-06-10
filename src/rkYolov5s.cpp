#include <stdio.h>
#include <mutex>
#include "rknn/rkYolvo5s.hpp"


const int RK3588 = 3;

// 功能：获取内核数量NPU
int get_core_num()
{
    static int core_num = 0; // 生命周期：程序开始到程序结束。在全局/静态区
    static std::mutex mtx;
    
    std::unique_lock<std::mutex> lock(mtx);

    int temp = core_num % RK3588;
    core_num ++ ;
    return temp;

}

// 功能：将张量的维度信息拼接成字符串并输出
static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    // yolov5的输出应该是（1,3,80,80,85）（1,3,40,40,85）（1,3,20,20,85）
    std::string shape_str = attr->n_dims < 1 ? "" : std::to_string(attr->dims[0]);
    for(uint32_t i = 1; i < attr->n_dims; i++)
    {
        shape_str += "," + std::to_string(attr->dims[i]);
    }
    printf("dims = [%s]\n", shape_str.c_str()); 
}

// 功能：加载数据。从文件中，参数分别为文件，偏移量，读取大小， 返回值是无符号char类型的指针
static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
    unsigned char *data; // 用于存储目标数据
    int ret; // 用于判断返回值

    data = NULL; // 初始化为空指针，是避免出现野指针

    if(fp == NULL) 
    {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET); // 将文件指针 fp 移动到文件的指定偏移量 ofst 处, SEEK_SET表示从文件开头
    if (ret != 0) // 定位成功才返回0
    {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if( data == NULL)
    {
        printf("buffer malloc failure. \n");
        return NULL;
    }

    ret = fread(data, 1, sz, fp); // 从fp中读取sz大小的字节到data中，每次读取1字节。ret表示实际读取的数量
    return data;


}

// 功能：加载模型。从文件名为filename的文件中，加载模型，加载的字节个数为model_size。
static unsigned char *load_model(const char *filename, int *model_size)
{
    FILE *fp;
    unsigned char *data;
    fp = fopen(filename, "rb");

    if(fp == NULL)
    {
        printf("Open file %s failed \n",filename); // %s输出文件名，%p输出地址
        return NULL;
    }

    fseek(fp, 0, SEEK_END);

    int size = ftell(fp); // ftell 函数用于获取当前文件指针的位置 

    data = load_data(fp, 0, size); // SEEK_END 表示从文件末尾开始计算偏移量,指针 fp 移动到文件的末尾

    fclose(fp);

    *model_size = size; // 解引用指针，model_size中存储的地址，解引用是访问该地址的实际值。
    return data;
} // open -> seek -> malloc -> read -> close


//功能：向指定文件写入指定大小的数据  fprintf(写入的文件，写入的内容)
static int saveFloat(const char *filename, float *output, int element_size)
{
    FILE *fp;
    fp = fopen(filename,"w");

    for(int i = 0; i < element_size; i++)
    {
        fprintf(fp, "%.6f\n", output[i]);
    }
    fclose(fp);
    return 0;
}

// std::ifstream 中的 i 代表输入，意思是数据从文件流入程序，in入程序
// std::ofstream 中的 o 代表输出，意味着数据从程序流出到文件 out出程序
// 功能：加载标签文件, std::ifstream（用于从文件读取数据）、std::ofstream（用于向文件写入数据）
void rkYolov5s::load_label(const std::string &label_path, std::vector<std::string> &labels)
{
    std::ifstream infile(label_path); //从文件中读取数据
    if(!infile.is_open())
    {
        std::cout << "Error opening file: " << label_path << std::endl;
        return;
    }

    std::string line;
    while(std::getline(infile, line)) // 获取一行一行的
    {
        labels.push_back(line);
    }
}

// 功能：构造函数
rkYolov5s::rkYolov5s(const std::string &model_path, const std::string &label_path)
{
    this->model_path = model_path;
    this->label_path = label_path;
    this->conf_threshold = CONF_THRESHOLD; // 问题：原代码没有使用this，应该是个bug
    this->nms_threshold = NMS_THRESHOLD; // 不重名时不用this，重名时使用this
}


// ctx： RKNN 模型相关的上下文环境，操作都基于该上下文来进行
// 调用： 你到底是被谁调用了啊，找不到调用位置
// 功能：类初始化
int rkYolov5s::init(rknn_context *ctx_in, bool share_weight) // 是否共享权重（多线程下的复用）
{
    // 加载模型数据
    std::cout << "start init rkYolov5s" << std::endl;
    printf("Loading model %s\n", model_path.c_str()); 
    int model_data_size = 0;
    model_data = load_model(model_path.c_str(), &model_data_size);


    // 模型初始化或者复用
    if(share_weight == true)
        ret = rknn_dup_context(ctx_in, &ctx); // 已初始化的上下文（由 rknn_init() 创建）作为源，即ctx_in,所以下面是else
    else   
        ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL); //初始化rknn模型对象
    if(ret < 0)
    {
        printf("rknn_init error ret = %d\n", ret);
        return -1;
    }

    // 加载模型标签并绑定NPU核心
    load_label(label_path, labels);
    rknn_core_mask core_mask;
    switch (get_core_num())
    {
        case 0:
            core_mask = RKNN_NPU_CORE_0; //1
            break;
        case 1:
            core_mask = RKNN_NPU_CORE_1; //2
            break;
        case 2:
            core_mask = RKNN_NPU_CORE_2; //4
    }
    ret = rknn_set_core_mask(ctx, core_mask); // 指定函数的NPU核心
    if( ret < 0)
    {
        printf("rknn_init core error ret=%d\n", ret);
        return -1;
    }


    // 查询相关信息：版本和输入输出数量
    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version)); //查询NPU版本，为version赋值
    if( ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)); // 查询输入输出的数量（个数非维度）
    if( ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, model output num: %d\n", io_num.n_input, io_num.n_output);


    // 设置输入输出的参数  读取输入张量结构体的属性信息
    input_attrs = (rknn_tensor_attr *)calloc(io_num.n_input, sizeof(rknn_tensor_attr)); // 动态分配内存
    for(uint32_t i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i; 
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr)); // 查询输入参数属性
        if( ret < 0)
        {
            printf("rknn_init error ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&(input_attrs[i])); // 输出张量的输入维度信息，函数接受的是指针，使用&。 *解引用得到的是结构体实例
        // dims = [1,640,640,3]
        // input_attrs是指向结构体数组的指针，每个元素是结构体实例。
    }

    output_attrs = (rknn_tensor_attr *)calloc(io_num.n_output, sizeof(rknn_tensor_attr));
    for(uint32_t i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(output_attrs[i]));
        // dims = [1,21,80,80]
        // dims = [1,21,40,40]
        // dims = [1,21,20,20]
    }

    if(input_attrs[0].fmt == RKNN_TENSOR_NCHW) // 只对input_attrs[0]进行设置
    {
        printf("model is NCHW input fmt \n"); // dims[0]表示的是n
        channel = input_attrs[0].dims[1];
        height = input_attrs[0].dims[2];
        width = input_attrs[0].dims[3];
    }
    else{
        printf("model is NHWC input fmt \n");
        channel = input_attrs[0].dims[3];
        height = input_attrs[0].dims[1];
        width = input_attrs[0].dims[2];
    }
    printf("model input channel = %d, height = %d, width = %d\n",channel, height, width);


    // 设置输入的属性
    memset(inputs, 0, sizeof(inputs)); 
    inputs[0].index = 0; //设置输入的索引
    inputs[0].type = RKNN_TENSOR_INT8; //设置输入类型为int8
    inputs[0].size = width * height * channel ; // 输入的大小
    inputs[0].fmt = RKNN_TENSOR_NHWC; // 输入数据格式 nhwc
    inputs[0].pass_through = 0; // 处理模式：不启动直通模式

    return 0;
}

// 功能：获取 RKNN 模型相关的上下文环境
rknn_context *rkYolov5s::get_pctx()
{
    return &ctx; // 类成员，返回ctx地址，因为返回的是指针类型
}

//功能：推理，输入：inputData 输出：dataEncode
dataEncode rkYolov5s::infer(inputData input_frame_data)
{
    std::cout << "start rkYolov5s infer" << std::endl;
    // 初始化数据
    dataEncode data_encode;
    data_encode.frame_time_stamp = input_frame_data.frame_time_stamp;

    // 创建原图对象
    cv::Mat img;
    img = input_frame_data.frame_data; // 原图
    data_encode.image = input_frame_data.frame_data; // 原图赋值给输出结构体
    img_width = img.cols; // 宽度：列
    img_height = img.rows; // 高度 行
    BOX_RECT pads;
    memset(&pads, 0, sizeof(BOX_RECT));

    // 创建目标图对象
    cv::Size target_size(width, height); // 宽度，高度  640*640
    cv::Mat resized_img(target_size.height, target_size.width, CV_8UC3); // rows，cols，8位无符号三通道

    // 获取缩放比例（原值 * scale = 目标值）
    float scale_w = (float) target_size.width / img.cols;
    float scale_h = (float) target_size.height / img.rows;
    float min_scale = std::min(scale_w, scale_h); 
    scale_h = min_scale;
    scale_w = min_scale;


    // 对目标图对象赋值, 将数据保存到输入对象rknn_input中
    cv::Scalar pad_color = cv::Scalar(0, 0, 0);
    if(img_width != width || img_height != height)
    {
        letterbox(img, resized_img, pads, min_scale, target_size, pad_color);
        inputs[0].buf = resized_img.data; // 图像的数据的地址赋值 返回一个指向矩阵数据的 ​常量指针
    }
    else
    {
        inputs[0].buf = img.data;
    }

    // 将输入数据 inputs 设置到 RKNN 模型的上下文 ctx 中并创建输出结构体
    rknn_inputs_set(ctx, io_num.n_output, inputs); // 设置输入数据
    rknn_output outputs[io_num.n_output]; // 存储模型的输出结果
    memset(outputs, 0, sizeof(outputs));
    for(uint32_t i = 0; i < io_num.n_output; i++)
    {
        outputs[i].want_float = 0; // 不需要将输出结果转换为浮点数类型
    }

    // 模型推理
    ret = rknn_run(ctx, NULL);
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL); // 获取输出到outputs中

    // 后处理
    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;  // 存储缩放因子和零点
    for(uint32_t i = 0; i < io_num.n_output; i++ )
    {
        out_scales.push_back(output_attrs[i].scale);
        out_zps.push_back(output_attrs[i].scale); // 问题：这些值哪来的？什么时候加载到张量属性中的
    }
    post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, height, width, conf_threshold,
     nms_threshold, pads, scale_w, scale_h, out_zps, out_scales, &data_encode.detect_result_group, labels); // letterbox时获取到了pads

    ret = rknn_outputs_release(ctx, io_num.n_output, outputs); // 释放模型输出结果占用的资源
    data_encode.detect_result_group.id = input_frame_data.frame_index;

    return data_encode;

}

// 功能：析构函数，销毁所有的堆分配空间
rkYolov5s::~rkYolov5s()
{
    deinitPostProcess();
    ret = rknn_destroy(ctx); // 销毁 RKNN（Rockchip Neural Network）模型的上下文环境 ctx

    if(model_data)
        free(model_data); // data = (unsigned char *)malloc(sz);
    
    if(input_attrs)
        free(input_attrs); // input_attrs = (rknn_tensor_attr *)calloc(io_num.n_input, sizeof(rknn_tensor_attr)); 

    if(output_attrs)
        free(output_attrs);
}
