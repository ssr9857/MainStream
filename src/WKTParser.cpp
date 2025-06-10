#include "WKTParser.h"
#include <iostream>
#include <exception>

// typedef boost::geometry::model::d2::point_xy<double> Boost_Point;
// 主要用于判断一个点或者矩阵是否在一个多边形内

WKTParser::WKTParser(const cv::Size &size) : m_size(size) {

}

WKTParser::~WKTParser() {

}

bool WKTParser::read_wkt_point(const std::string& wkt, point& pt) {
    std::vector<std::string> tokens;
    // 按照空格和括号分割 WKT 格式字符串
    size_t pos_begin = wkt.find_first_of("("), pos_end = 0;
    if (pos_begin == std::string::npos) {
        // 缺少左括号，解析失败
        return false;
    }

    pos_end = wkt.find_first_of(" )", pos_begin + 1);
    while (pos_end != std::string::npos) {
        tokens.push_back(wkt.substr(pos_begin + 1, pos_end - pos_begin - 1));
        pos_begin = pos_end;
        pos_end = wkt.find_first_of(")", pos_begin + 1);
    }
    if (tokens.size() != 2) {
        // 属性数量不正确，解析失败
        return false;
    }

    // 从分割出来的子串中提取坐标
    try {
        pt.x = std::stod(tokens[0]);
        pt.y = std::stod(tokens[1]);
    } catch (...) {
        // 坐标解析失败，解析失败
        return false;
    }

    // 解析成功
    return true;
}

bool WKTParser::parsePoint(const std::string &src, cv::Point *pointPtr) {
    // Boost_Point bp;
    point bp;
    try {
        read_wkt_point(src, bp);
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return false;
    }

    m_points.emplace_back(bp.x * m_size.width, bp.y * m_size.height);

    if (pointPtr) {
        pointPtr->x = bp.x * m_size.width;
        pointPtr->y = bp.y * m_size.height;
    }

    return true;
}

bool WKTParser::parseLinestring(const std::string &src, VectorPoint *pvp) {
    std::cout << "no parseLinestring implement" << std::endl;
//     boost::geometry::model::linestring<Boost_Point> linstring;
//     try {
//         boost::geometry::read_wkt(src, linstring);
//     } catch (std::exception &e) {
//         std::cerr << e.what() << std::endl;
//         return false;
//     }

//     VectorPoint vp;
//     for (auto &item : linstring) {
//         vp.emplace_back(item.x() * m_size.width, item.y() * m_size.height);
//     }
//     m_lines.emplace_back(vp);

//     if (pvp) {
//         *pvp = vp;
//     }

    return true;
}

bool WKTParser::parsePolygon(const std::string& src, VectorPoint* pvp) {
    std::string wkt = src;
    std::vector<std::vector<double>> points;

    // 解析 WKT 格式字符串
    size_t pos_begin = wkt.find_first_of("("), pos_end = 0;
    if (pos_begin == std::string::npos) {
        // 缺少左括号，解析失败
        return false;
    }

    pos_begin++;
    pos_end = wkt.find_first_of(",)", pos_begin + 1);
    while (pos_end != std::string::npos) {
        std::string token = wkt.substr(pos_begin + 1, pos_end - pos_begin - 1);
        if(token.empty())
            break;
        token = "(" + token + ")";
        point pt;
        if (!read_wkt_point(token, pt)) {
            // 解析当前点失败，解析失败
            return false;
        }
        points.push_back({pt.x, pt.y});

        pos_begin = pos_end;
        pos_end = wkt.find_first_of(",)", pos_begin + 1);
    }

    if (points.size() < 4 || points.front() != points.back()) {
        // 点数量不正确或首尾点不相等，不是合法的多边形，解析失败
        return false;
    }

    VectorPoint vp(points.size());
    for (size_t i = 0; i < points.size(); i++) {
        vp[i].x = points[i][0] * m_size.width;
        vp[i].y = points[i][1] * m_size.height;
    }
    m_polygons.push_back(vp);

    if (pvp) {
        *pvp = vp;
    }

    return true;
}

bool WKTParser::inPolygons(const cv::Point &point) {
    if (empty()) return false;

    for (auto iter = m_polygons.cbegin(); iter != m_polygons.cend(); iter++) {
        if (WKTParser::inPolygon(*iter, point)) {
            return true;
        }
    }

    return false;
}

bool WKTParser::inPolygons(const cv::Rect &rect) {
    if (empty()) return false;

    for (auto iter = m_polygons.cbegin(); iter != m_polygons.cend(); iter++) {
        if (WKTParser::inPolygon(*iter, rect)) {
            return true;
        }
    }

    return false;
}

bool WKTParser::polygon2Rect(const VectorPoint &polygon, cv::Rect &rect) {
    int min_x, min_y, max_x, max_y;
    min_x = min_y = std::numeric_limits<int>::max();
    max_x = max_y = std::numeric_limits<int>::min();

    for (size_t i = 0; i < polygon.size(); i++) {
        if (polygon[i].x < min_x) min_x = polygon[i].x;
        if (polygon[i].x > max_x) max_x = polygon[i].x;
        if (polygon[i].y < min_y) min_y = polygon[i].y;
        if (polygon[i].y > max_y) max_y = polygon[i].y;
    }

    rect.x = min_x;
    rect.y = min_y;
    rect.width = max_x - min_x;
    rect.height = max_y - min_y;

    return true;
}

bool WKTParser::inPolygon(const VectorPoint &polygon, const cv::Point &point) {
    cv::Point2f pf(point.x, point.y);
    return (cv::pointPolygonTest(polygon, pf, false) >= 0);
}

bool WKTParser::inPolygon(const VectorPoint &polygon, const cv::Rect &rect) {
    return (WKTParser::inPolygon(polygon, rect.tl()) &&
            WKTParser::inPolygon(polygon, cv::Point(rect.x + rect.width, rect.y)) &&
            WKTParser::inPolygon(polygon, rect.br()) &&
            WKTParser::inPolygon(polygon, cv::Point(rect.x, rect.y + rect.height)));
}
