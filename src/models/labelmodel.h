#pragma once

#include "storage/labelstore.h"

#include <QAbstractListModel>

namespace Fjordpost {

// Alle Labels mit Anzahl der Nachrichten (für Seitenleiste und Label-Menü).
class LabelModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        LabelIdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        CountRole,
    };

    explicit LabelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void reload();

Q_SIGNALS:
    void countChanged();

private:
    QList<Label> m_labels;
    QHash<qint64, int> m_counts;
};

} // namespace Fjordpost
