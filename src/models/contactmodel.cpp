#include "contactmodel.h"

namespace Fjordpost {

ContactModel::ContactModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

void ContactModel::setSearch(const QString &text)
{
    if (m_search == text)
        return;
    m_search = text;
    reload();
    Q_EMIT searchChanged();
}

void ContactModel::reload()
{
    beginResetModel();
    m_items = DavStore::contacts(m_search);
    m_books.clear();
    const auto books = DavStore::collections(0, QStringLiteral("addressbook"));
    for (const DavCollection &book : books)
        m_books.insert(book.id, book);
    endResetModel();
    Q_EMIT countChanged();
}

int ContactModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant ContactModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const DavItem &item = m_items.at(index.row());
    switch (role) {
    case ItemIdRole:
        return item.id;
    case NameRole:
        return item.title;
    case SubtitleRole:
        return item.subtitle;
    case EmailRole:
        return item.emails.value(0);
    case LetterRole:
        return item.title.isEmpty() ? QString() : item.title.left(1).toUpper();
    case BookColorRole:
        return DavStore::displayColor(m_books.value(item.collectionId));
    case BookNameRole:
        return m_books.value(item.collectionId).name;
    default:
        return {};
    }
}

QHash<int, QByteArray> ContactModel::roleNames() const
{
    return {{ItemIdRole, "itemId"}, {NameRole, "name"}, {SubtitleRole, "subtitle"}, {EmailRole, "email"}, {LetterRole, "letter"}, {BookColorRole, "bookColor"}, {BookNameRole, "bookName"}};
}

} // namespace Fjordpost
