/*
 * TagSearchEngine 实现：n-gram 哈希嵌入 + HNSW 近邻检索
 * 详见 tagsearchengine.h 顶部的设计说明。
 */

#include "tagsearchengine.h"

// hnswlib 为纯头文件库；包含路径由 .pro 的 INCLUDEPATH 指向 third_party
#include "hnswlib/hnswlib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// 稳定的 FNV-1a 哈希：对特征字符串（UTF-16 码元序列）做散列。
// 用确定性哈希而非 std::hash，保证不同运行/平台结果一致，便于调试。
uint32_t fnv1a(const QString &feature)
{
    uint32_t h = 2166136261u;                 // FNV offset basis
    for (const QChar ch : feature) {
        h ^= static_cast<uint32_t>(ch.unicode());
        h *= 16777619u;                        // FNV prime
    }
    return h;
}

} // namespace

TagSearchEngine::TagSearchEngine() = default;

// 析构需放在 .cpp：此处 hnswlib 类型已完整定义，unique_ptr 才能正确销毁
TagSearchEngine::~TagSearchEngine() = default;

std::vector<float> TagSearchEngine::embed(const QString &text) const
{
    // 结果向量：每一维是落到该桶的所有特征的累加权重
    std::vector<float> vec(kDim, 0.0f);

    // 归一化：小写化（对中文为 no-op，对英文统一大小写）。
    // 标签串以 '|' 分隔，统一替换为空格作为“分段边界”，
    // 这样二元组（bigram）不会跨越两个不相关的标签。
    QString norm = text.toLower();
    norm.replace('|', ' ');

    // 按空白切分成若干“词段”，逐段提取单字与相邻二元组特征。
    const QStringList segments = norm.split(QChar(' '), Qt::SkipEmptyParts);
    for (const QString &seg : segments) {
        const int n = seg.size();
        for (int i = 0; i < n; ++i) {
            // 单字特征（unigram）：捕捉单个字符层面的重合
            const QString unigram = QStringLiteral("u:") + seg.at(i);
            vec[fnv1a(unigram) % kDim] += 1.0f;

            // 相邻二元组特征（bigram）：捕捉“流行/行音/音乐”这类局部组合，
            // 中文逐字 + 二字组合能较好地表达标签之间的相近程度。
            if (i + 1 < n) {
                const QString bigram = QStringLiteral("b:") + seg.at(i) + seg.at(i + 1);
                vec[fnv1a(bigram) % kDim] += 1.0f;
            }
        }
    }

    // L2 归一化：归一化后向量的内积即为余弦相似度，契合 InnerProductSpace。
    double sumSq = 0.0;
    for (float v : vec) sumSq += static_cast<double>(v) * v;
    if (sumSq > 0.0) {
        const float inv = static_cast<float>(1.0 / std::sqrt(sumSq));
        for (float &v : vec) v *= inv;
    }
    // 无任何特征时返回零向量：build() 不会索引它；search() 中它与任何点的
    // 相似度都约为 0，会被阈值过滤掉，从而安全地返回“无匹配”。
    return vec;
}

void TagSearchEngine::build(const QVector<QPair<qint64, QString>> &items)
{
    // 整表重建：先丢弃旧索引
    m_index.reset();
    m_space.reset();
    m_count = 0;

    // 先筛出“有标签”的资源；空标签资源不参与检索
    QVector<QPair<qint64, QString>> valid;
    valid.reserve(items.size());
    for (const auto &it : items) {
        if (!it.second.trimmed().isEmpty())
            valid.push_back(it);
    }
    if (valid.isEmpty())
        return; // 没有可索引的数据，保持空索引

    // 建立内积空间与 HNSW 索引。max_elements 取有效资源数即可。
    m_space = std::make_unique<hnswlib::InnerProductSpace>(kDim);
    m_index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
        m_space.get(),
        static_cast<size_t>(valid.size()),
        /*M=*/16,
        /*ef_construction=*/200);

    // 逐个把资源标签嵌入成向量并加入索引，label 直接用资源 id
    for (const auto &it : valid) {
        const std::vector<float> v = embed(it.second);
        m_index->addPoint(v.data(), static_cast<hnswlib::labeltype>(it.first));
        ++m_count;
    }
}

QList<qint64> TagSearchEngine::search(const QString &query, int topK) const
{
    QList<qint64> result;

    // 空查询 / 空索引：直接返回空，由调用方决定是否回退为“显示全部”
    if (query.trimmed().isEmpty() || !m_index || m_count == 0)
        return result;

    // 查询向量
    const std::vector<float> q = embed(query);

    // k 不能超过索引内元素数；适当抬高 ef 以保证召回质量
    const int k = std::min(topK, m_count);
    if (k <= 0)
        return result;
    m_index->setEf(std::max(64, k));

    // searchKnn 返回“距离最大者在堆顶”的优先队列；距离 = 1 - 余弦相似度。
    auto pq = m_index->searchKnn(q.data(), static_cast<size_t>(k));

    // 先收集 (相似度, id)，再按相似度从高到低排序并过滤阈值
    QVector<QPair<float, qint64>> hits;
    hits.reserve(static_cast<int>(pq.size()));
    while (!pq.empty()) {
        const float sim = 1.0f - pq.top().first;        // 距离 → 相似度
        const qint64 id = static_cast<qint64>(pq.top().second);
        pq.pop();
        if (sim >= kSimThreshold)                        // “差不多的才显示”
            hits.push_back({sim, id});
    }

    std::sort(hits.begin(), hits.end(),
              [](const QPair<float, qint64> &a, const QPair<float, qint64> &b) {
                  return a.first > b.first;              // 相似度高的排前面
              });

    for (const auto &h : hits)
        result.push_back(h.second);
    return result;
}
