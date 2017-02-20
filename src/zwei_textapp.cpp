/*
    Application logic for a command line/text based tool
 */

#if !defined(ZWEI_TEXTAPP_PROTOTYPES)
#define ZWEI_TEXTAPP_PROTOTYPES

#include "zwei_app.hpp"
#include "zwei_types.hpp"

struct ProcessedMessage {
        bool success;
        uint8_t sha1_digest[20];
        size_t fileindex;
        char const *filepath;
        char const *filename;
        uint64_t filesize;
        ZoeMailStoreFile zoefile;
        ByteCountedRange zoe_rel_url;
        MessageSummary message_summary;
};

struct Zwei {
        GetMessageSummaryFn get_message_summary;
        ParseZoeMailstoreFilenameFn parse_zoe_mailstore_filename;
};

zw_internal void process_message(Zwei const &zwei,
                                 char const *filename,
                                 char const *filepath,
                                 uint8_t const *full_message,
                                 uint8_t const *full_message_last,
                                 MemoryArena transient_arena,
                                 MemoryArena &result_arena,
                                 ProcessedMessage &destination);
zw_internal void
print_processed_message(ProcessedMessage const &processed_message,
                        MemoryArena transient_arena);
#endif

#if defined(ZWEI_TEXTAPP_IMPLEMENTATION)

#include "secure_hash_standard.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"

// TODO(nicolas): wrap spdr somehow so that we always have this global variable
// in every executable
#include "../modules/uu.spdr/include/spdr/spdr.hh"
extern SPDR_Context *global_spdr;

zw_internal void print_message_summary(MessageSummary const &message_summary,
                                       MemoryArena transient_arena)
{
        // FEATURE(nicolas): print raw content of From: To: CC
        // Sender: Message-Id: In-Reply-To: Subject:
        TextOutputGroup text_output_group = {};
        allocate(text_output_group, &transient_arena, KILOBYTES(16));

        // TODO(nicolas): having a delimited table printing abstraction
        // would help with these (and the app)

        auto print_field = [&text_output_group](const char *name,
                                                struct ByteCountedRange value) {
                if (value.first) {
                        push_back_cstr(text_output_group, "\t");
                        push_back_cstr(text_output_group, name);
                        push_back_cstr(text_output_group, "\t");
                        push_back_extent(text_output_group, value.first,
                                         value.count);
                        trace(text_output_group);
                }
        };
        auto print_bytes_list_field = [&text_output_group](
            const char *name, struct ByteCountedRange *values, size_t count) {
                if (count == 0) {
                        return;
                }

                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, name);
                push_back_cstr(text_output_group, "\t");
                algos::for_each_n(
                    values, count, [&](const ByteCountedRange &range) {
                            push_back_extent(text_output_group, range.first,
                                             range.count);
                    });
                trace(text_output_group);
        };
        // TODO(nicolas): mailboxes could be stored sorted,
        // since there is not a lot of semantic attached to them
        // however, what about groups?
        // The advantage of sorting mailboxes is that we could
        // compare groups of recipients for instance and name
        // them somehow
        auto print_mailbox_list_field = [&text_output_group](
            const char *name, RawMailbox const *mailboxes,
            size_t mailboxes_count) {
                if (mailboxes_count == 0) {
                        return;
                }
                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, name);
                push_back_cstr(text_output_group, "\t");

                auto push_back_mailbox = [](TextOutputGroup &text_output_group,
                                            RawMailbox const &mailbox) {
                        push_back_cstr(text_output_group, "<mailbox ");
                        if (mailbox.display_name_bytes.count > 0) {
                                push_back_cstr(text_output_group, "\"");
                                push_back_extent(
                                    text_output_group,
                                    mailbox.display_name_bytes.first,
                                    mailbox.display_name_bytes.count);
                                push_back_cstr(text_output_group, "\" ");
                        }
                        push_back_extent(text_output_group,
                                         mailbox.local_part_bytes.first,
                                         mailbox.local_part_bytes.count);
                        push_back_cstr(text_output_group, "@");
                        push_back_extent(text_output_group,
                                         mailbox.domain_bytes.first,
                                         mailbox.domain_bytes.count);
                        push_back_cstr(text_output_group, ">");
                };

                if (mailboxes_count > 0) {
                        push_back_mailbox(text_output_group, *mailboxes);
                        ++mailboxes;
                        --mailboxes_count;
                }

                algos::for_each_n(
                    mailboxes, mailboxes_count, [&](const RawMailbox &mailbox) {
                            push_back_cstr(text_output_group, ", ");
                            push_back_mailbox(text_output_group, mailbox);
                    });

                trace(text_output_group);
        };

        if (!message_summary.valid_rfc5322) {
                trace_print("\tRFC5322\tfalse");
        }

        print_field("MESSAGE-ID", message_summary.message_id_bytes);
        print_mailbox_list_field(
            "SENDER", &message_summary.sender_mailbox,
            message_summary.sender_mailbox.domain_bytes.count > 0 ? 1 : 0);
        {
                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, "ORIG_DATE");
                push_back_cstr(text_output_group, "\t");
                CivilDateTime const &value = message_summary.orig_date;
                push_back_formatted(
                    text_output_group, "<%d-%02d-%02dT%02d:%02d:%"
                                       "02d%c%02d:%02d>",
                    value.year, value.month_count, value.day_count, value.hour,
                    value.minute, value.seconds,
                    value.zone_hour_offset >= 0 ? '+' : '-',
                    abs(value.zone_hour_offset), value.zone_minute_offset);
                trace(text_output_group);
        }
        print_mailbox_list_field("FROM", message_summary.from_mailboxes,
                                 message_summary.from_mailboxes_count);
        print_mailbox_list_field("TO", message_summary.to_mailboxes,
                                 message_summary.to_mailboxes_count);
        print_mailbox_list_field("CC", message_summary.cc_mailboxes,
                                 message_summary.cc_mailboxes_count);
        print_bytes_list_field("IN_REPLY_TO",
                               message_summary.in_reply_to_msg_ids,
                               message_summary.in_reply_to_msg_ids_count);
        print_field("SUBJECT", message_summary.subject_field_bytes);
        {
                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, "CONTENT_TRANSFER_ENCODING");
                push_back_cstr(text_output_group, "\t");
                push_back_u32(text_output_group,
                              message_summary.content_transfer_encoding);
                trace(text_output_group);
        }
        print_field("FIRST_LINE", message_summary.first_line_bytes);
}

zw_internal void process_message(Zwei const &zwei,
                                 char const *filename,
                                 char const *filepath,
                                 uint8_t const *full_message,
                                 uint8_t const *full_message_last,
                                 MemoryArena transient_arena,
                                 MemoryArena &result_arena,
                                 ProcessedMessage &destination)
{
        SPDR_SCOPE1(global_spdr, "app", "process_message",
                    SPDR_STR("filepath", filepath));
        IOBufferIterator file_content;
        stream_on_memory(&file_content, const_cast<uint8_t *>(full_message),
                         full_message_last - full_message);
        uint8_t sha1_result[20];
        sha1(file_content.start, file_content.end - file_content.start,
             sha1_result);
        finish_iobuffer(&file_content);

        zw_assert(countof(destination.sha1_digest) == countof(sha1_result),
                  "defect");
        algos::copy_n(sha1_result, countof(sha1_result),
                      destination.sha1_digest);

        destination.filename = filename;
        destination.filepath = filepath;
        destination.filesize = full_message_last - full_message;

        auto zoefile_errorcode =
            zwei.parse_zoe_mailstore_filename(&destination.zoefile, filename);
        if (0 != zoefile_errorcode && !cstr_endswith(filepath, ".eml")) {
                return;
        }

        // TODO(nicolas): @platform_specific here we have encoded the
        // path separator, which makes this code non portable
        if (0 == zoefile_errorcode) {
                // find predecessor, until you reach
                // boundary
                auto pred = [](char const *const first, char const *const pos) {
                        if (pos != first) {
                                return algos::predecessor(pos);
                        }
                        return pos;
                };
                // skip day, month, year
                auto first = cstr_find_last(filepath, '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/' ? first : algos::successor(first);
                auto last = cstr_find_last(first, '.');
                destination.zoe_rel_url = {(uint8_t *)first,
                                           size_t(last - first)};
        }

        SPDR_BEGIN(global_spdr, "app", "get_message_summary");
        auto errorcode = zwei.get_message_summary(
            full_message, full_message_last,
            zoefile_errorcode == 0 ? &destination.zoefile : nullptr,
            &transient_arena, &result_arena, &destination.message_summary);
        zw_assert(errorcode == 0, "unexpected errorcode");
        SPDR_END(global_spdr, "app", "get_message_summary");

        destination.success = true;
}

zw_internal void
print_processed_message(ProcessedMessage const &processed_message,
                        MemoryArena transient_arena)
{
        SPDR_SCOPE2(global_spdr, "main", __FUNCTION__,
                    SPDR_STR("filepath", processed_message.filepath),
                    SPDR_INT("fileindex", int(processed_message.fileindex)));
        char const *filepath = processed_message.filepath;
        TextOutputGroup traceg = {};
        allocate(traceg, &transient_arena, KILOBYTES(1));
        {
                push_back_cstr(traceg, "FILE");
                push_back_formatted(traceg, "%lld",
                                    processed_message.fileindex);
                push_back_cstr(traceg, " ");
                push_back_cstr(traceg, filepath);
                push_back_cstr(traceg, ":");
                trace(traceg);
        }
        push_back_cstr(traceg, "SHA1");
        push_back_cstr(traceg, "\t");

        auto const &sha1_digest = processed_message.sha1_digest;
        char byteToHexChar[] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
        };
        for (size_t byteIndex = 0; byteIndex < countof(sha1_digest);
             byteIndex++) {
                uint8_t const byte = sha1_digest[byteIndex];
                push_back_formatted(traceg, "%c%c", byteToHexChar[byte >> 4],
                                    byteToHexChar[byte & 0xF]);
        }
        trace(traceg);

        if (processed_message.success) {
                // FEATURE(nicolas): print filing timestamp
                push_back_cstr(traceg, "PATH");
                push_back_cstr(traceg, "\t");
                push_back_cstr(traceg, filepath);
                trace(traceg);

                push_back_cstr(traceg, "SIZE");
                push_back_cstr(traceg, "\t");
                push_back_u64(traceg, processed_message.filesize);
                push_back_cstr(traceg, "\t");
                trace(traceg);

                push_back_cstr(traceg, "ZOE_REL_URL");
                push_back_cstr(traceg, "\t");
                push_back_extent(traceg, processed_message.zoe_rel_url.first,
                                 processed_message.zoe_rel_url.count);
                trace(traceg);
                // ID(a94812)
                push_back_cstr(traceg, "UUID");
                push_back_cstr(traceg, "\t");
                for (size_t i = 0; i < countof(processed_message.zoefile.uuid);
                     i++) {
                        push_back_formatted(traceg, "%x",
                                            processed_message.zoefile.uuid[i]);
                }
                trace(traceg);

                push_back_cstr(traceg, "UNIX TIMESTAMP");
                push_back_cstr(traceg, "\t");
                push_back_formatted(
                    traceg, "%llu",
                    processed_message.zoefile.unix_epoch_millis);
                trace(traceg);

                print_message_summary(processed_message.message_summary,
                                      transient_arena);
        } else {
                trace_print("ignored file");
        }
}
#endif
