#include "blobstore.h"

#include "database.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>

namespace Fjordpost::BlobStore {

QString absolutePath(const QString &relativePath)
{
    return QDir(Database::dataDir()).filePath(QStringLiteral("blobs/") + relativePath);
}

QString store(qint64 accountId, const QString &category, const QByteArray &data)
{
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Zwei Ebenen Unterverzeichnisse, damit kein Ordner zu groß wird.
    const QString relative = QStringLiteral("%1/%2/%3/%4.eml").arg(accountId).arg(category, name.left(2), name);
    const QString path = absolutePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(data);
    return file.commit() ? relative : QString();
}

QByteArray load(const QString &relativePath)
{
    if (relativePath.isEmpty())
        return {};
    QFile file(absolutePath(relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool remove(const QString &relativePath)
{
    return !relativePath.isEmpty() && QFile::remove(absolutePath(relativePath));
}

} // namespace Fjordpost::BlobStore
