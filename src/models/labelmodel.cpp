#include "labelmodel.h"

namespace Fjordpost {

LabelModel::LabelModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

void LabelModel::reload()
{
    beginResetModel();
    m_labels = LabelStore::labels();
    m_counts = LabelStore::counts();
    endResetModel();
    Q_EMIT countChanged();
}

int LabelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_labels.size());
}

QVariant LabelModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Label &l = m_labels.at(index.row());
    switch (role) {
    case LabelIdRole:
        return l.id;
    case Qt::DisplayRole:
    case NameRole:
        return l.name;
    case ColorRole:
        return l.color;
    case CountRole:
        return m_counts.value(l.id);
    }
    return {};
}

QHash<int, QByteArray> LabelModel::roleNames() const
{
    return {
        {LabelIdRole, "labelId"},
        {NameRole, "name"},
        {ColorRole, "labelColor"},
        {CountRole, "messageCount"},
    };
}

} // namespace Fjordpost
