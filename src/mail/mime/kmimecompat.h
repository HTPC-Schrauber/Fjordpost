#pragma once

#include <KMime/Message>

#include <memory>

// Die API von KMime hat sich beim Wechsel von KDE PIM zu den Frameworks geändert.
namespace Fjordpost::KMimeCompat {

inline QByteArray decodedBody(const KMime::Content *content)
{
#ifdef FJORDPOST_KMIME_FRAMEWORK
    return content->decodedBody();
#else
    return content->decodedContent();
#endif
}

// parent übernimmt den Besitz von part.
inline void appendPart(KMime::Content &parent, KMime::Content *part)
{
#ifdef FJORDPOST_KMIME_FRAMEWORK
    parent.appendContent(std::unique_ptr<KMime::Content>(part));
#else
    parent.appendContent(part);
#endif
}

inline QByteArray encodedWithCrlf(const KMime::Content &content)
{
#ifdef FJORDPOST_KMIME_FRAMEWORK
    return content.encodedContent(KMime::NewlineType::CRLF);
#else
    return content.encodedContent(true);
#endif
}

} // namespace Fjordpost::KMimeCompat
