#include "resourcesearch.h"

void ResourceSearch::setAllResources(const QStringList &all)
{
    m_all = all;
}

QStringList ResourceSearch::filter(const QString &keyword) const
{
    QString k = keyword.trimmed();
    if(k.isEmpty()) return m_all;

    QStringList out;
    for(const QString &name : m_all)
    {
        // 包含匹配（不区分大小写）
        if(name.contains(k, Qt::CaseInsensitive))
            out << name;
    }
    return out;
}