/*
 * TagSearchEngine：基于 HNSW 近邻算法的“标签近似搜索”引擎
 * ------------------------------------------------------------
 * 设计目标（低耦合）：
 *   - 本类只依赖 hnswlib（纯头文件）与 Qt 基础容器，不引用任何业务结构
 *     （如 SessionDto）。调用方只需把 (资源id, 标签串) 喂进来，再用查询串
 *     拿回匹配的资源 id 列表，完全不关心内部的向量化 / 近邻检索细节。
 *
 * 核心思路：
 *   HNSW 做的是“向量空间近邻检索”，因此必须先把文本变成定长向量。
 *   本项目是纯离线客户端，没有可用的语义 embedding 模型，所以这里用
 *   “字符 n-gram 哈希编码”（hashing trick）作为离线、确定性的“嵌入”：
 *   把标签/查询拆成单字 + 相邻二元组，哈希散列到固定维度并 L2 归一化。
 *   归一化后向量的“内积 == 余弦相似度”，正好对应 hnswlib 的内积空间。
 *
 *   这是一种“字面/词形相似度”（非深层语义），但它是离线唯一可行、且能
 *   真正驱动 HNSW 的方案，足以满足“标签差不多的资源都显示出来”的需求。
 *
 * 若日后接入真实语义 embedding（例如服务端模型），只需替换 embed() 的
 * 实现，build() / search() 的接口与整套 HNSW 检索流程都无需改动。
 */

#ifndef TAGSEARCHENGINE_H
#define TAGSEARCHENGINE_H

#include <QString>
#include <QVector>
#include <QList>
#include <QPair>

#include <vector>
#include <memory>

// 仅在头文件中前向声明 hnswlib 类型，避免把第三方头文件传染给所有包含者
namespace hnswlib {
    template <typename T> class HierarchicalNSW;
    class InnerProductSpace;
}

class TagSearchEngine
{
public:
    TagSearchEngine();
    ~TagSearchEngine();

    // 用 (资源id, 标签串) 列表构建/重建索引。
    // 标签串为上传时的原始格式（多个标签以 '|' 分隔），空标签的资源会被跳过
    // （它们不会出现在任何搜索结果里）。数据每次变化时整表重建即可。
    void build(const QVector<QPair<qint64, QString>> &items);

    // 按查询串检索：返回“标签相近”的资源 id，按相似度从高到低排序。
    //   - query 去除首尾空白后为空 → 返回空列表（调用方据此回退为“显示全部”）。
    //   - 仅保留相似度 >= 内部阈值的结果，从而实现“差不多的才显示”。
    //   - topK 限制最多返回的候选数量。
    QList<qint64> search(const QString &query, int topK = 50) const;

private:
    // 把任意文本编码为定长归一化向量（本引擎的“嵌入”核心算法）。
    std::vector<float> embed(const QString &text) const;

    // 向量维度：兼顾区分度与性能；取 16 的倍数以便 hnswlib 走 SIMD 优化路径。
    static constexpr int kDim = 256;
    // 相似度阈值（余弦相似度，范围约 [0,1]）：低于此值视为“不相近”而被过滤。
    static constexpr float kSimThreshold = 0.15f;

    std::unique_ptr<hnswlib::InnerProductSpace> m_space;     // 距离度量（内积空间）
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> m_index; // HNSW 索引
    int m_count = 0;                                          // 已入索引的资源数
};

#endif // TAGSEARCHENGINE_H
