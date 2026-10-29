#pragma once

#include <QtGlobal>

namespace Fjordpost {

// Bitmaske für messages.flags (angelehnt an die IMAP-Systemflags).
namespace MessageFlag {
enum : int {
    Seen = 1 << 0,
    Flagged = 1 << 1,
    Answered = 1 << 2,
    Draft = 1 << 3,
    Deleted = 1 << 4,
    Forwarded = 1 << 5,
};
}

// Spezielle Ordnerrollen (RFC 6154 SPECIAL-USE + Posteingang).
namespace FolderRole {
inline constexpr auto Inbox = "inbox";
inline constexpr auto Sent = "sent";
inline constexpr auto Drafts = "drafts";
inline constexpr auto Trash = "trash";
inline constexpr auto Junk = "junk";
inline constexpr auto Archive = "archive";
inline constexpr auto Flagged = "flagged"; // nur virtuell (Unified-Ansicht)
}

} // namespace Fjordpost
