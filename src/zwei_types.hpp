#pragma once

#include "date_time_types.hpp"
#include "mail_types.hpp"

#include <cstdint>

enum MaildirFlag {
        MaildirFlag_Passed = 1 << 0,
        MaildirFlag_Replied = 1 << 2,
        MaildirFlag_Seen = 1 << 3,
        MaildirFlag_Trashed = 1 << 4,
        MaildirFlag_Draft = 1 << 5,
        MaildirFlag_User = 1 << 6,
};

struct ZoeMailStoreFile {
        uint8_t uuid[16];
        uint64_t unix_epoch_millis;
        uint64_t maildir_flags; // Bitfield(MaildirFlag)
};

struct ByteCountedRange {
        uint8_t *first;
        size_t count;
};

struct RawMailbox {
        ByteCountedRange display_name_bytes;
        ByteCountedRange local_part_bytes;
        ByteCountedRange domain_bytes;
};

struct MessageSummary {
        /// true when the message is fully rfc5322 conformant
        bool valid_rfc5322;

        ByteCountedRange message_id_bytes;

        ByteCountedRange *in_reply_to_msg_ids;
        size_t in_reply_to_msg_ids_count;

        RawMailbox *from_mailboxes;
        size_t from_mailboxes_count;

        RawMailbox sender_mailbox;

        RawMailbox *to_mailboxes;
        size_t to_mailboxes_count;

        RawMailbox *cc_mailboxes;
        size_t cc_mailboxes_count;

        ByteCountedRange subject_field_bytes;

        ContentTransferEncodingType content_transfer_encoding;

        ByteCountedRange first_line_bytes;
        // TODO(nicolas): add references list
        CivilDateTime orig_date;
};
