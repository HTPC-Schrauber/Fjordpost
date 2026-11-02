#pragma once

#include "storage/davstore.h"

#include <QAbstractListModel>

namespace Fjordpost {

// Kontakte der sichtbaren Adressbücher, sortiert und durchsuchbar.
class ContactModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        NameRole,
        SubtitleRole,
        EmailRole,
        LetterRole,
        BookColorRole,
        BookNameRole,
    };

    explicit ContactModel(QObject *parent = nullptr);

    QString search() const { return m_search; }
    void setSearch(const QString &text);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void reload();

Q_SIGNALS:
    void searchChanged();
    void countChanged();

private:
    QList<DavItem> m_items;
    QHash<qint64, DavCollection> m_books;
    QString m_search;
};

} // namespace Fjordpost
