#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <set>
#include "rknn/postprocess.h"

// static char *labels[OBJ_CLASS_NUM];

const int anchor0[6] = {10, 13, 16, 30, 33, 23};
const int anchor1[6] = {30, 61, 62, 45, 59, 119};
const int anchor2[6] = {116, 90, 156, 198, 373, 326};

// 内联函数：编译器会尝试将函数调用处用函数体本身的代码来替换，不进行栈帧的创建、参数传递、返回值处理等一系列操作。(只是一个建议,不强制)
// 功能：限制值在一个范围内【min, max】 触发隐式类型转换，min转换为float然后进行比较
inline int clamp(float val, int min ,int max) 
{
    return val > min ? (val < max ? val : max) : min; 
}

// 传入的参数的坐标坐标原点（0，0）在左上角，这在计算时就迎刃而解了
// 功能：计算IOU交并比
float CalculateOverlop(float x0min, float y0min, float x0max, float y0max, float x1min, float y1min, float x1max, float y1max)
{
    // fmax(返回较大的参数，默认是双精度double，使用0.f则认为是单精度)
    // 处理的是像素，而不是简单的浮点数，所以要+1.0
    float w = fmax(0.f, fmin(x0max, x1max) - fmax(x0min, x1min) + 1.0);
    float y = fmax(0.f, fmin(y0max, y1max) - fmax(y0min, y1min) + 1.0);
    float iou_area = w*y;
    float sum_area = (x0max - x0min + 1.0)*(y0max - y0min + 1.0) + (x1max - x1min + 1.0)*(y1max - y1min + 1.0) - iou_area;
    return sum_area <= 0.f ? 0.f : (iou_area / sum_area);

}

// 功能： 非极大值抑制
// 有效个数：validCount = 3
// 边界框信息：outputLocations = {10,10,20,20,30,30,40,40,50,50,60,60} 左上角坐标，宽高
// 框对应的类别id classIds = {0,2,0}
// 框索引 order = {0,1,2}
// 过滤类别  检测指定类别 filterId = 2
// IOU阈值 threshold = 0.45
int nms(int validCount, std::vector<float> &outputLocations, std::vector<int> classIds, std::vector<int> &order,
            int filterId, float threshold)
{
    for(int i = 0; i < validCount; i++)
    {
        if(order[i] == -1 || (classIds[i] != filterId)) // order中的值为经过概率值排序后的索引顺序，例如【3,1,2】对应的概率值【0.7,0.6,0.5】
        {
            continue;
        }
        int n = order[i];
        for(int j = i+1 ;j < validCount; j++)
        {
            int m = order[j];
            if(order[j] == -1 || (classIds[j] != filterId))
            {
                continue;
            }
            // outputLocations中的值依次为左上角坐标和宽高
            float x0min = outputLocations[n*4 + 0]; // 这里的n指的是处理后的第n个box
            float y0min = outputLocations[n*4 + 1];
            float x0max = outputLocations[n*4 + 0] + outputLocations[n*4 + 2];
            float y0max = outputLocations[n*4 + 1] + outputLocations[n*4 + 3];

            float x1min = outputLocations[m*4 + 0];
            float y1min = outputLocations[m*4 + 1];
            float x1max = outputLocations[m*4 + 0] + outputLocations[m*4 + 2];
            float y1max = outputLocations[m*4 + 1] + outputLocations[m*4 + 3];
            // 传入第一个框：左上角和右下角坐标， 第二个框的左上角和右上角坐标
            float iou = CalculateOverlop(x0min, y0min, x0max, y0max, x1min, y1min, x1max, y1max);
            if(iou > threshold)
            {
                order[m] = -1;
            }
        }
    }
    return 0;
}

// 功能：根据得到的最终概率值进行排序
// 原概率值为【0.7,0.8，0.6】 索引列表为【1,2,3】
// 根据排序后得到的索引列表为 【2,1,3】
int quick_sort_indice_inverse(std::vector<float> &input, int left, int right, std::vector<int> &indices)
{
  float key;
  int key_index;
  int low = left;
  int high = right;
  if (left < right)
  {
    key_index = indices[left];
    key = input[left];
    while (low < high)
    {
      while (low < high && input[high] <= key)
      {
        high--;
      }
      input[low] = input[high];
      indices[low] = indices[high];
      while (low < high && input[low] >= key)
      {
        low++;
      }
      input[high] = input[low];
      indices[high] = indices[low];
    }
    input[low] = key;
    indices[low] = key_index;
    quick_sort_indice_inverse(input, left, low - 1, indices);
    quick_sort_indice_inverse(input, low + 1, right, indices);
  }
  return low;
}

// 功能：激活函数将输入值映射到（0,1）
float sigmoid(float x)
{
    return 1.0 / (1.0 + expf(-x)); 
}
// 功能：计算 Sigmoid 函数的反函数值, 由输出拿到输入值
float unsgmoid(float y)
{
    return -1.0 * (logf((1.0 / y) - 1.0)); 
}

// typedef int  int32_t;
// 功能：限制值范围
inline int32_t __clip(float val, float min ,float max)
{
    return val > min ? (val < max ? val : max) : min;
}

// typedef signed char int8_t;
// 功能：进行量化操作  Q = （R / S） + Z； S是缩放因子（缩放因子由参数中的浮点数最大小值（非浮点数可表示最大小值）和-128,127确定）
int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + zp;
    int8_t res = __clip(dst_val, -128, 127);
    return res;
}

//功能：反仿射量化   int32位就是int类型
float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale)
{
    float res = (qnt - zp) * scale;
    return res;
}


// 功能：对输出的80*80*255的数据进行处理，拿到对应的x.y,w,h和conf以及该框对应的类，同时删去小于thresh_conf的框
// anchor中的是框的宽高，gird_h指的是第一个检测头得到的gird的高度，同理gird_w
// stride指的是步长，其实就是缩放比例，80*80的缩放8倍，40*40的是16倍，20*20的是32倍，sigmoid后
// 中心点坐标：y[..., 0:2] = (y[..., 0:2] * 2 - 0.5 + self.grid[i]) * self.stride[i] 
// gird是位置信息，例如第一个位置则是self.grid = [0,0]，第二个就是self.grid = [0,1]
// 宽高：y[..., 2:4] = (y[..., 2:4] * 2) ** 2 * self.anchor_grid[i] 
int process(int8_t *input, int *anchor, int gird_h, int gird_w, int height, int width, int stride, std::vector<float> &boxs,
    std::vector<float> &objProbs, std::vector<int> &classId, float threshold_conf, int32_t zp, float scale, std::vector<std::string> labels)
{
    int validConut = 0;
    int gird_len = gird_h * gird_w;
    int8_t thres_i8 = qnt_f32_to_affine(threshold_conf, zp, scale);
    for(int a = 0; a < 3; a++ )
    {
        for(int i = 0; i < gird_h; i++ )
        {
            for(int j = 0; j < gird_w; j++ )
            {
                int8_t box_confidence = input[((5 + labels.size())*a + 4)*gird_len + i*gird_w + j]; //从input中读取置信度值
                if(box_confidence >= thres_i8)// 这么多框进行第一步删选
                {
                    int offset = ((5 + labels.size())*a)*gird_len + i*gird_w + j;
                    int8_t *in_ptr = input + offset; // 创建一个指针，用于定位目标框的位置
                    // 获取第a层的第（i,j）位置的框的坐标和宽高
                    float box_x = (deqnt_affine_to_f32(in_ptr[0], zp, scale))*2.0 - 0.5;
                    float box_y = (deqnt_affine_to_f32(in_ptr[gird_len], zp, scale))*2.0 - 0.5;
                    float box_w = (deqnt_affine_to_f32(in_ptr[gird_len * 2], zp, scale))*2.0;
                    float box_h = (deqnt_affine_to_f32(in_ptr[gird_len * 3], zp, scale))*2.0;
                    box_x = (box_x + j) * stride;// y[..., 0:2] = (y[..., 0:2] * 2 - 0.5 + self.grid[i]) * self.stride[i] 
                    box_y = (box_y + i) * stride;
                    box_w =  box_w * box_w * anchor[a * 2];// y[..., 2:4] = (y[..., 2:4] * 2) ** 2 * self.anchor_grid[i]
                    box_h = box_h * box_h * anchor[a * 2 + 1];

                    int8_t maxClassProbs = in_ptr[5 * gird_len]; //int8_t是因为输出结果就是这个类型
                    int maxClassId = 0; // 这俩定义了初始的概率值和位置
                    for(int k = 1; k < static_cast<int>(labels.size()); k++)
                    {
                        int8_t prob = in_ptr[(5 + k) * gird_len]; //从input中读取概率值
                        if(prob > maxClassProbs)
                        {
                            maxClassProbs = prob;
                            maxClassId = k;
                        }
                    }
                    if(maxClassProbs > thres_i8) // 进行第二步删选，合格的拿走
                    // 这里不应该是大于具体类别的类别概率吗？
                    {
                        objProbs.push_back((deqnt_affine_to_f32(maxClassProbs, zp, scale)) * (deqnt_affine_to_f32(box_confidence, zp, scale)));
                        validConut++;
                        classId.push_back(maxClassId);
                        boxs.push_back(box_x);
                        boxs.push_back(box_y);
                        boxs.push_back(box_w);
                        boxs.push_back(box_h);
                    }
                }
            }
        }
    }
    return validConut;
}


int post_process(int8_t *input0, int8_t *input1, int8_t *input2, int model_in_h, int model_in_w, float conf_threshold, float nms_threshold,
                BOX_RECT pads, float scale_w, float scale_h, std::vector<int32_t> &qnt_zps, std::vector<float> &qnt_scales,
                 detect_result_group_t *group, std::vector<std::string> labels)
{
    // memset(group, 0, sizeof(detect_result_group_t));
    *group = detect_result_group_t();
    std::vector<float> Boxs; // 过滤后得到的box框的中心点坐标和宽高
    std::vector<float> objProbs; // 经过处理后得到的概率值
    std::vector<int> classIds; // 经过处理后得到的概率值最大的类别ID

    // stride = 8
    int stride0 = 8;
    int grid_h0 = model_in_h / stride0; // 80*80
    int grid_w0 = model_in_h / stride0;
    int validCount0 = 0; // qnt_zps[0], qnt_scales[0]量化所用
    process(input0, (int *)anchor0, grid_h0, grid_w0, model_in_h, model_in_w,stride0, Boxs, objProbs, classIds, conf_threshold, qnt_zps[0], qnt_scales[0], labels); 

    
    // stride = 16
    int stride1 = 16;
    int grid_h1 = model_in_h / stride1; // 40*40
    int grid_w1 = model_in_h / stride1;
    int validCount1 = 0; // qnt_zps[1], qnt_scales[1]量化所用
    process(input1, (int *)anchor1, grid_h1, grid_w1, model_in_h, model_in_w, stride1, Boxs, objProbs, classIds, conf_threshold, qnt_zps[1], qnt_scales[1], labels); 


    // stride = 32
    int stride2 = 32;
    int grid_h2 = model_in_h / stride2; //20*20
    int grid_w2 = model_in_h / stride2;
    int validCount2 = 0; // qnt_zps[2], qnt_scales[2]量化所用
    process(input2, (int *)anchor2, grid_h2, grid_w2, model_in_h, model_in_w, stride2, Boxs, objProbs, classIds, conf_threshold, qnt_zps[2], qnt_scales[2], labels); 

    int validCount = validCount0 + validCount1 + validCount2;

    if( validCount <= 0)
    {
        std::cout << "没有符合的box" << std::endl;
        return 0;
    }

    std::vector<int> indexArrays; // 创建每个box的索引
    for(int i = 0; i < validCount; i++){
        indexArrays.push_back(i);
    }

    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArrays); // 根据概率值进行快排，并得到排序后对应的索引位置

    // 因为经过初始处理后不是每个类别都被保存下来了box
    // 从 classId 这个整数向量中提取出所有唯一的整数值，并存入一个 std::set<int> 容器里.
    std::set<int> class_set(std::begin(classIds), std::end(classIds)); // 遍历classIds从开始到结束
    
    for(auto c : class_set)
    {
        nms(validCount, Boxs, classIds, indexArrays, c, nms_threshold);
    }

    int last_count = 0;
    group->count = 0;
    for(int i = 0;i < validCount; i++)
    {
        if(indexArrays[i] == -1 || last_count >= OBJ_NUMS_MAX_SIZE) //在nms中indexArrays就是order
        {
            continue;
        }
        int n = indexArrays[i]; // 第indexArrays[i]个处理后的box框，一个个遍历
        // 这里面存储的好像是 框距离原图四个边的距离？ 而不是框在原图的位置，不知道是否理解正确。
        float x1 = Boxs[n * 4 + 0] - pads.left;  //问题：到底是中心点坐标和宽高，还是左上角坐标和宽高 (应该是中心点坐标的)
        float y1 = Boxs[n * 4 + 1] - pads.top;
        // float x1 = Boxs[n * 4 + 0] - Boxs[n * 4 + 2] / 2 - pads.left;
        // float y1 = Boxs[n * 4 + 1] - Boxs[n * 4 + 3] / 2 - pads.top;
        float x2 = x1 + Boxs[n * 4 + 2]; 
        float y2 = x2 + Boxs[n * 4 + 3]; // 分别表示左上角和右下角坐标  注：坐标第一次转换
        float obj_conf = objProbs[n];
        int id = classIds[n];

        group->results[last_count].box.left = (int)(clamp(x1, 0, model_in_w) / scale_w); // （目标值 / 缩放比例 = 原值）原图中对应的值
        group->results[last_count].box.top = (int)(clamp(y1, 0, model_in_h) / scale_h);
        group->results[last_count].box.right = (int)(clamp(x2, 0, model_in_w) / scale_w);
        group->results[last_count].box.bottom = (int)(clamp(y2, 0, model_in_h) / scale_h);
        // （x1,y1）左上角坐标    （x2,y2）右下角坐标
        group->results[last_count].prop = obj_conf;
        // char *strncpy(char *dest, const char *src, size_t n); 目标地址，源地址，最大长度
        strncpy(group->results[last_count].name, labels[id].c_str(), OBJ_NAME_MAX_SIZE);
        last_count++;
    }
    group->count = last_count;
    
    return 0;

}


void deinitPostProcess()
{
    std::cout << "deinitPostProcess" << std::endl;
}



//验证反仿射函数deqnt_affine_to_f32
// int main()
// {
//     int8_t qnt = 85;
//     float scale = 0.5;
//     int32_t zp = 2;
//     std::cout << deqnt_affine_to_f32(qnt, zp, scale) << std::endl;
    
// }


// 验证CalculateOverlop()函数，传入的是依次两个框的左上角右下角坐标（0点坐标在左上角）
// int main()
// {
//     float x0min = 1.0;
//     float y0min = 1.0;
//     float x0max = 2.0;
//     float y0max = 2.0;

//     float x1min = 2.0;
//     float y1min = 2.0;
//     float x1max = 3.0;
//     float y1max = 3.0;

//     float iou = CalculateOverlop(x0min, y0min, x0max, y0max, x1min, y1min, x1max, y1max);
//     std::cout << "交并比为" << iou << std::endl; // 交并比为0.142857
//     g++ postprocess.cpp -o test -I ./include ：头文件引用了，需要指定路径，不然报错
// }