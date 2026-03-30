#ifndef RESOURCESEARCH_H
#define RESOURCESEARCH_H

#include <QStringList>

class ResourceSearch
{
public:
    //设置完整资源列表（来自服务端LIST）
    void setAllResources(const QStringList &all);

    //关键词过滤：返回匹配到的列表
    //keyword 为空时返回全部
    QStringList filter(const QString &keyword) const;

private:
    QStringList m_all;
};

#endif // RESOURCESEARCH_H
