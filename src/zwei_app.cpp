/**

  # Main project todo list

  ## Source Connectors
  POP, IMAP, Twitter, Facebook etc...

  ## Input Parsing
  TODO(nicolas): Parse First Line Of Content (RFC5322)

  ## Database
  - Timeline indexing/querying

  ## User Interface
  ## Platform
  TODO(nicolas): work task queue
  TODO(nicolas): LINUX port.
  TODO(nicolas): cross compilation to <powerpc e500 v2> (Synology NAS)

  # MIME format
  ## References:

  ### Message Headers and Bodies

  RFC 5322 -- Internet Message Format
  https://tools.ietf.org/rfc/rfc5322.txt

  RFC 2045 -- MIME part 1: Format of Internet Message Bodies
  https://tools.ietf.org/rfc/rfc2045.txt

  RFC 2047 -- MIME part 3: Message Header Extensions for Non-ASCII text
  http://tools.ietf.org/rfc/rfc2047.txt

  RFC 2046 -- MIME part 2: Media Types

  RFC 2049 -- MIME part 5: Conformance Criteria and Examples
  RFC 2231 -- MIME Parameter Value and Encoded Word Extensions: Character Sets,
  Languages, and Continuations
  RFC 3676 -- The Text/Plain Format and DelSp Parameters
  RFC 3798 -- Message Disposition Notification
  RFC 4289 -- MIME  part 4: Registration Procedures
  RFC 5147 -- URI Fragment Identifiers for the text/plain Media Type
  RFC 6532 -- Internationalized Email Headers
  RFC 6838 -- Media Type Specifications and Registration Procedures
  RFC 6657 -- Update to MIME regarding "charset" Parameter Handling in Textual
  Media Types

  NOTES(nicolas): it seems rather frequent for foreign language operators
  to have 8-bit characters in their From: One would assume that they
  then used the charset that was in the Content-Type header.

  Quite a bit of my mails from my Sylpheed period are in that form!

  TODO(nicolas) form a simple blacklist that would allow cleaning the
  database. (Charsets for instance?)

  TODO(nicolas) add user time line (technical counterpart: filing date
  in UNIX_TIMESTAMP)

  TODO(nicolas): fix weird files in my maildirs which have a mailbox
  like prefix (From ...) at the top.
  1999/10/28/1DE7CF6800DBABF02A931D3D98D5B0A7.eml

  TODO(nicolas): my own mails sent by recognition sometimes put 8bit
  in the headers. See "JavaMail.Nicolas@Recognition" and for example
  2015/1/15/EF8AF508014AA7B30E7DB0475AB1742E.eml
*/

#include "zwei_inlines.hpp"

#include "zwei_app.hpp"

#include "algos.hpp"
#include "base64.hpp"
#include "rfc2047.hpp"
#include "rfc5234.hpp"
#include "rfc5322.hpp"
#include "zoe.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_nicolas.hpp"
#include "zwei_types.hpp"

#include <cstddef>    // for offsetof
#include <functional> // for std::cref (TODO(nicolas): can I write it myself?)

zw_internal bool ucs4_to_macintosh(uint32_t const *codepoints_first,
                                   uint32_t const *codepoints_last,
                                   uint8_t *destination);

/// @see
/// [economical-utf8.html](http://bjoern.hoehrmann.de/utf-8/decoder/dfa/index.html)
enum { UTF8_ACCEPT = 0,
       UTF8_REJECT = 1,
};

zw_internal uint8_t const utf8d[] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   9,   9,   9,   9,   9,   9,
    9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   7,   7,   7,   7,   7,
    7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,
    7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   8,   8,   2,
    2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
    2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   0xa,
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x4, 0x3, 0x3,
    0xb, 0x6, 0x6, 0x6, 0x5, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x8, 0x0, 0x1, 0x2, 0x3, 0x5, 0x8, 0x7, 0x1, 0x1, 0x1, 0x4, 0x6, 0x1, 0x1,
    0x1, 0x1, 1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   0,   1,   1,   1,   1,   1,   0,   1,   0,   1,   1,
    1,   1,   1,   1,   1,   2,   1,   1,   1,   1,   1,   2,   1,   2,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   2,   1,   1,   1,   1,   1,   1,   1,
    2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   3,
    1,   3,   1,   1,   1,   1,   1,   1,   1,   3,   1,   1,   1,   1,   1,
    3,   1,   3,   1,   1,   1,   1,   1,   1,   1,   3,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
};

/**
 * decodes UTF-8 streams.
 *
 * @param state must be UTF8_ACCEPT initially
 */
zw_internal uint32_t utf8_decode(uint32_t *state, uint32_t *codep, uint8_t byte)
{
        uint32_t type = utf8d[byte];

        *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6)
                                         : (0xff >> type) & (byte);

        *state = utf8d[256 + *state * 16 + type];

        return *state;
}

// NOTE(nicolas) the following is a work
// around for some bad transcoding that
// happened in the past. Our corpus contains
// iso8859/utf8/8bit content that was badly transcoded:
//
// original data - interpreted as MacRoman ->
// re-encoded as UTF-8 when I ran ZOE on
// OSX. Java used to default to the MacRoman
// encoding.
zw_internal uint8_t *macroman_workaround_block(struct MacRomanWorkaround *state,
                                               uint8_t *block_first,
                                               uint8_t *block_last)
{
        using algos::begin;
        using algos::end;

        auto decoded_block_last = block_first;
        auto codepoint_first = begin(state->ucs4_codepoints_block);
        auto codepoint_buffer_last = end(state->ucs4_codepoints_block);
        auto codepoint_last = codepoint_first;

        {
                auto decode_codepoint = [state](uint8_t x) {
                        return UTF8_ACCEPT ==
                               utf8_decode(&state->utf8decoder_state,
                                           &state->utf8decoder_codepoint, x);
                };
                auto decode_result = algos::apply_copy_bounded_if(
                    block_first, block_last, codepoint_first,
                    codepoint_buffer_last, [state](const uint8_t) {
                            return state->utf8decoder_codepoint;
                    }, decode_codepoint);

                if (UTF8_REJECT == state->utf8decoder_state) {
                        return nullptr;
                }
                decoded_block_last = decode_result.first;
                codepoint_last = decode_result.second;
        }

        auto const macintosh_encoding_result = ucs4_to_macintosh(
            codepoint_first, codepoint_last, state->macintosh_chars);
        zw_assert(macintosh_encoding_result, "could not interpret as MacRoman");

        if (!macintosh_encoding_result) {
                return nullptr;
        }

        // Test if it already is utf8 content
        algos::for_each(codepoint_first, codepoint_last, [state](uint8_t x) {
                utf8_decode(&state->macintosh_chars_utf8decoder_state,
                            &state->macintosh_chars_utf8decoder_codepoint, x);
        });
        bool const is_already_utf8 =
            UTF8_REJECT != state->macintosh_chars_utf8decoder_state;

        auto const destination_first = begin(state->utf8_output_block);
        auto const destination_last = end(state->utf8_output_block);

        size_t destination_count = 0;

        if (is_already_utf8) {
                auto result = algos::copy_bounded(
                    begin(state->macintosh_chars), end(state->macintosh_chars),
                    destination_first, destination_last);
                zw_assert(result.first == end(state->macintosh_chars),
                          "stepped out of destination");
                destination_count = result.second - destination_first;
        } else {
                using algos::source;
                using algos::sink;
                using algos::successor;

                // it's not, assume it's latin1
                auto destination = destination_first;
                auto macintosh_chars = begin(state->macintosh_chars);
                auto macintosh_chars_last = end(state->macintosh_chars);
                while (destination != destination_last &&
                       macintosh_chars != macintosh_chars_last) {
                        auto x = source(macintosh_chars);
                        if (x < 0x0080) {
                                sink(destination) = x;
                        } else /* NOTE(nicolas) < 0x7ff */ {
                                zw_assert(destination + 1 != destination_last,
                                          "stepping out");
                                uint32_t mask = (1 << 6) - 1;
                                sink(destination) = 0xc0 | ((x >> 6) & mask);
                                destination = successor(destination);
                                sink(destination) = 0x80 | ((x >> 0) & mask);
                        }
                        destination = successor(destination);
                        macintosh_chars = successor(macintosh_chars);
                }
                destination_count = destination - destination_first;
        }

        state->utf8_output_block_count = destination_count;

        return decoded_block_last;
}

// wraps a stream and transcode using the MacRoman workaround
zw_internal struct BufferRange
macroman_workaround_stream(struct BufferRange *input, struct MemoryArena *arena)
{
        struct Stream {
                BufferRange *input;
                MacRomanWorkaround state;
        } *stream = push_pointer_rvalue(arena, stream);

        *stream = (struct Stream){
            input, {UTF8_ACCEPT, 0, 0, {}, {}, UTF8_ACCEPT, 0, {}, 0},
        };

        auto next = [](struct BufferRange *range) {
                struct Stream *stream = reinterpret_cast<struct Stream *>(
                    range->start -
                    offsetof(struct Stream, state.utf8_output_block));
                if (stream->input->cursor >= stream->input->end) {
                        if (BR_NoError != stream->input->next(stream->input)) {
                                return fail(range, stream->input->error);
                        }
                }

                auto input_cursor = macroman_workaround_block(
                    &stream->state, stream->input->cursor, stream->input->end);
                if (!input_cursor) {
                        return fail(
                            range,
                            BR_IOError); // TODO(nicolas) not really I/O but..
                }

                stream->input->cursor = input_cursor;

                range->start = &stream->state.utf8_output_block[0];
                range->end =
                    range->start + stream->state.utf8_output_block_count;
                range->cursor = range->start;

                return BR_NoError;
        };

        struct BufferRange range = {
            &stream->state.utf8_output_block[0],
            &stream->state.utf8_output_block[0],
            &stream->state.utf8_output_block[0],
            BR_NoError,
            next,
        };

        next(&range);

        return range;
}

struct RawHeaders {
        ByteCountedRange message_id_bytes;
        struct ByteCountedRange *in_reply_to_msg_ids;
        size_t in_reply_to_msg_ids_count;
        // TODO(nicolas) add references list

        RawMailbox *from_mailboxes;
        size_t from_mailboxes_count;
        RawMailbox sender_mailbox;
        RawMailbox *to_mailboxes;
        size_t to_mailboxes_count;
        RawMailbox *cc_mailboxes;
        size_t cc_mailboxes_count;

        struct ByteCountedRange subject_field_bytes;

        // TODO(nicolas) add missing original date as a concrete type
        struct ByteCountedRange content_transfer_encoding_bytes;
        struct ByteCountedRange text_content_subtype_bytes;
        struct ByteCountedRange text_content_charset_bytes;
};

struct MessageBeingParsed {
        struct MemoryArena *arena;

        bool is_rfc5322; /// true when the message is fully rfc5322 conformant

        enum { UNPARSED,
               RAW_HEADERS,
        } content_state;
        struct RawHeaders raw_content;
};

zw_global RFC5322 mail_parsers;
zw_global bool global_debug_mode;

extern "C" EXPORT INIT_APP(init_app)
{
        trace_print("Initializing app");
        if (flags & ZWEI_DEBUG_MODE_FLAG) {
                global_debug_mode = true;
        }

        extern void hammer_init();

        hammer_init();
        auto &rfc5234 = make_rfc5234();
        auto &base64 = make_base64(rfc5234);
        auto &rfc2045 = make_rfc2045(rfc5234);
        auto &rfc2047 = make_rfc2047(rfc5234, rfc2045, base64);
        mail_parsers = make_rfc5322(rfc5234, rfc2047);
        zoe_support_init();
}

zw_internal void fill_raw_headers(RawHeaders *raw_headers,
                                  const HParsedToken *ast,
                                  MemoryArena *arena)
{
        *raw_headers = {};

        auto collect = [&raw_headers, arena](HParsedToken const *token) {
                enum { NONE,
                       BYTES,
                       BYTES_LIST,
                       MAILBOX,
                       MAILBOX_LIST } process = NONE;
                ByteCountedRange *d_bytes_range = nullptr;
                ByteCountedRange **d_bytes_ranges = nullptr;
                size_t *d_bytes_ranges_count = nullptr;
                RawMailbox *d_mailbox = nullptr;
                RawMailbox **d_mailboxes = nullptr;
                size_t *d_mailboxes_count = nullptr;

                if (RFC5322TokenIs(token, MESSAGE_ID_FIELD)) {
                        process = BYTES;
                        d_bytes_range = &raw_headers->message_id_bytes;
                } else if (RFC5322TokenIs(token, SUBJECT_FIELD)) {
                        process = BYTES;
                        d_bytes_range = &raw_headers->subject_field_bytes;
                } else if (RFC5322TokenIs(token, FROM_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &raw_headers->from_mailboxes;
                        d_mailboxes_count = &raw_headers->from_mailboxes_count;
                } else if (RFC5322TokenIs(token, TO_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &raw_headers->to_mailboxes;
                        d_mailboxes_count = &raw_headers->to_mailboxes_count;
                } else if (RFC5322TokenIs(token, CC_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &raw_headers->cc_mailboxes;
                        d_mailboxes_count = &raw_headers->cc_mailboxes_count;
                } else if (RFC5322TokenIs(token, SENDER_FIELD)) {
                        process = MAILBOX;
                        d_mailbox = &raw_headers->sender_mailbox;
                } else if (RFC5322TokenIs(token, IN_REPLY_TO_FIELD)) {
                        process = BYTES_LIST;
                        d_bytes_ranges = &raw_headers->in_reply_to_msg_ids;
                        d_bytes_ranges_count =
                            &raw_headers->in_reply_to_msg_ids_count;
                }

                switch (process) {
                case BYTES_LIST: {
                        zw_assert(d_bytes_ranges, "d_bytes_ranges");
                        zw_assert(d_bytes_ranges_count, "d_bytes_ranges_count");

                        auto bytes_ranges_size =
                            rfc5322_field_get_bytes_array_size(token);
                        ByteCountedRange *ranges = push_array_rvalue(
                            arena, ranges, bytes_ranges_size.count);
                        uint8_t *bytes = push_array_rvalue(
                            arena, bytes, bytes_ranges_size.extra_bytes_count);
                        rfc5322_field_copy_bytes_array(token, ranges, bytes);
                        *d_bytes_ranges = ranges;
                        *d_bytes_ranges_count = bytes_ranges_size.count;
                } break;
                case BYTES: {
                        zw_assert(d_bytes_range, "d_bytes_range");

                        size_t bytes_count = rfc5322_field_bytes_count(token);
                        uint8_t *arena_bytes =
                            push_array_rvalue(arena, arena_bytes, bytes_count);
                        uint8_t *last_bytes =
                            rfc5322_field_copy_bytes(token, arena_bytes);
                        zw_assert((size_t)(last_bytes - arena_bytes) ==
                                      bytes_count,
                                  "unexpected byte count");
                        *d_bytes_range = {
                            arena_bytes, bytes_count,
                        };
                } break;
                case MAILBOX_LIST: {
                        zw_assert(d_mailboxes, "d_mailboxes");
                        zw_assert(d_mailboxes_count, "d_mailboxes_count");

                        auto mailbox_list_size =
                            rfc5322_field_get_mailbox_array_size(token);
                        RawMailbox *mailboxes = push_array_rvalue(
                            arena, mailboxes, mailbox_list_size.count);
                        uint8_t *bytes = push_array_rvalue(
                            arena, bytes, mailbox_list_size.extra_bytes_count);

                        auto mailbox_end = rfc5322_field_copy_mailbox_array(
                            token, mailboxes, bytes);
                        zw_assert((size_t)(mailbox_end - mailboxes) ==
                                      mailbox_list_size.count,
                                  "unexpected number of copied "
                                  "mailboxes");
                        *d_mailboxes = mailboxes;
                        *d_mailboxes_count = mailbox_list_size.count;
                } break;
                case MAILBOX: {
                        zw_assert(d_mailbox, "d_mailbox");
                        auto mailbox_list_size =
                            rfc5322_field_get_mailbox_array_size(token);
                        if (mailbox_list_size.count == 1) {
                                uint8_t *bytes = push_array_rvalue(
                                    arena, bytes,
                                    mailbox_list_size.extra_bytes_count);
                                rfc5322_field_copy_mailbox_array(
                                    token, d_mailbox, bytes);
                        }
                } break;
                case NONE:
                        break;
                }
        };
        algos::traverse_each(rfc5322_top(ast), std::cref(collect));
}

extern "C" EXPORT ACCEPT_MIME_MESSAGE(accept_mime_message)
{
        struct BufferRange *message_range = range;
        bool must_print_ast = global_debug_mode;

        // TODO(nicolas) refine when/how this workaround is applied It
        // really belongs to a per-user library of corpus
        // correction. Corpus correction facilities does not belong
        // within the app as it's highly corpus specific. And also
        // likely to be one-time-use.
        //
        //  Example: for each sha1: blacklist, transcoding methods.

        bool const must_apply_macroman_workaround =
            zoe_mailstore_file &&
            zoe_mailstore_file->unix_epoch_millis < 1095552000000LL;

        if (must_apply_macroman_workaround) {
                trace_print("applying MacRoman workaround");
                struct BufferRange *message_decoder =
                    push_pointer_rvalue(message_arena, message_decoder);
                *message_decoder =
                    macroman_workaround_stream(range, message_arena);
                message_range = message_decoder;
        }

        uint8_t *full_message = (uint8_t *)push_bytes(message_arena, 0);
        uint8_t *full_message_end = full_message;

        while (message_range->error == BR_NoError) {
                zw_assert(full_message_end == push_bytes(message_arena, 0),
                          "non contiguous");
                full_message_end =
                    algos::copy(message_range->start, message_range->end,
                                (uint8_t *)push_bytes(
                                    message_arena,
                                    message_range->end - message_range->start));

                message_range->next(message_range);
        }

        if (message_range->error != BR_ReadPastEnd) {
                error_print("unknown I/O error while parsing message");
                return;
        }

        struct MessageBeingParsed message_parsed = {
            NULL /* was message_arena */,
            true,
            MessageBeingParsed::UNPARSED,
            {},
        };
        // Hammer parsing
        {
                // NOTE(nicolas): we can use the "fields" parser if we
                // want to be less strict.
                auto result = h_parse(mail_parsers.fields, full_message,
                                      full_message_end - full_message);
                if (!result) {
                        trace_print("PARSE ERROR");
                        return;
                }

                zw_assert(result, "could not parse message with hammer");
                if (result) {
                        if (must_print_ast) {
                                rfc5322_print_ast(stdout, result->ast, 0, 4);
                        }
                        message_parsed.is_rfc5322 =
                            rfc5322_validate(result->ast);
                        fill_raw_headers(&message_parsed.raw_content,
                                         result->ast, message_arena);

                        message_parsed.content_state =
                            MessageBeingParsed::RAW_HEADERS;

                        HArenaStats stats;
                        h_allocator_stats(result->arena, &stats);
                        h_parse_result_free(result);
                }

                // TODO(nicolas) detect if there was an error.. After all
                // we just slurped the header or at least some part of
                // the header.. what about reacting to that?
        }

        if (!message_parsed.is_rfc5322) {
                trace_print("\tRFC5322: ERROR");
        }
        if (message_parsed.content_state == MessageBeingParsed::RAW_HEADERS) {
                // FEATURE(nicolas): print raw content of From: To: CC
                // Sender: Message-Id: In-Reply-To: Subject:
                TextOutputGroup text_output_group = {};
                allocate(text_output_group, message_arena, KILOBYTES(16));

                // TODO(nicolas): having a delimited table printing abstraction
                // would help with these (and the app)

                auto raw = message_parsed.raw_content;
                auto print_field = [&text_output_group](
                    const char *name, struct ByteCountedRange value) {
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
                    const char *name, struct ByteCountedRange *values,
                    size_t count) {
                        if (count == 0) {
                                return;
                        }

                        push_back_cstr(text_output_group, "\t");
                        push_back_cstr(text_output_group, name);
                        push_back_cstr(text_output_group, "\t");
                        algos::for_each_n(
                            values, count, [&](const ByteCountedRange &range) {
                                    push_back_extent(text_output_group,
                                                     range.first, range.count);
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
                    const char *name, RawMailbox *mailboxes,
                    size_t mailboxes_count) {
                        if (mailboxes_count == 0) {
                                return;
                        }
                        push_back_cstr(text_output_group, "\t");
                        push_back_cstr(text_output_group, name);
                        push_back_cstr(text_output_group, "\t");

                        auto push_back_mailbox = [](
                            TextOutputGroup &text_output_group,
                            RawMailbox const &mailbox) {
                                push_back_cstr(text_output_group, "<mailbox ");
                                if (mailbox.display_name_bytes.count > 0) {
                                        push_back_cstr(text_output_group, "\"");
                                        push_back_extent(
                                            text_output_group,
                                            mailbox.display_name_bytes.first,
                                            mailbox.display_name_bytes.count);
                                        push_back_cstr(text_output_group,
                                                       "\" ");
                                }
                                push_back_extent(
                                    text_output_group,
                                    mailbox.local_part_bytes.first,
                                    mailbox.local_part_bytes.count);
                                push_back_cstr(text_output_group, "@");
                                push_back_extent(text_output_group,
                                                 mailbox.domain_bytes.first,
                                                 mailbox.domain_bytes.count);
                                push_back_cstr(text_output_group, ">");
                        };

                        if (mailboxes_count > 0) {
                                push_back_mailbox(text_output_group,
                                                  *mailboxes);
                                ++mailboxes;
                                --mailboxes_count;
                        }

                        algos::for_each_n(
                            mailboxes, mailboxes_count,
                            [&](const RawMailbox &mailbox) {
                                    push_back_cstr(text_output_group, ", ");
                                    push_back_mailbox(text_output_group,
                                                      mailbox);
                            });
                        trace(text_output_group);
                };

                // TODO(nicolas): @feature print the first line of the message.
                print_field("MESSAGE-ID", raw.message_id_bytes);
                print_mailbox_list_field(
                    "SENDER", &raw.sender_mailbox,
                    raw.sender_mailbox.domain_bytes.count > 0 ? 1 : 0);
                print_mailbox_list_field("FROM", raw.from_mailboxes,
                                         raw.from_mailboxes_count);
                print_mailbox_list_field("TO", raw.to_mailboxes,
                                         raw.to_mailboxes_count);
                print_mailbox_list_field("CC", raw.cc_mailboxes,
                                         raw.cc_mailboxes_count);
                print_bytes_list_field("IN_REPLY_TO", raw.in_reply_to_msg_ids,
                                       raw.in_reply_to_msg_ids_count);
                print_field("SUBJECT", raw.subject_field_bytes);

                print_field("CONTENT ENCODING",
                            raw.content_transfer_encoding_bytes);
                print_field("TEXT_SUBTYPE", raw.text_content_subtype_bytes);
                print_field("TEXT CHARSET", raw.text_content_charset_bytes);
        }

        return;
}

extern "C" EXPORT PARSE_ZOE_MAILSTORE_PATH(parse_zoe_mailstore_path)
{
        auto zr = zoe_parse_uuid_filename(filename, cstr_len(filename));
        if (failed(zr)) {
                return -1;
        }

        *result = just(zr);

        return 0;
};

/**

   Encode ucs4 codepoints into "Mac OS Roman" 8-bit encoding.
   characters outside of the encodable range are substituted to by
   '?' (63) if any character had to be substituted, will return false
   to indicate an incomplete encoding.

 */
zw_internal bool ucs4_to_macintosh(uint32_t const *codepoints_first,
                                   uint32_t const *codepoints_last,
                                   uint8_t *destination)
{
        bool encoding_was_total = true;

        auto encode = [](uint32_t codepoint) -> uint32_t {
                if (codepoint < 0x80) {
                        return codepoint & 0xff;
                } else {
                        switch (codepoint) {
                        case 0x00c4:
                                return 0x80;
                        case 0x00c5:
                                return 0x81;
                        case 0x00c7:
                                return 0x82;
                        case 0x00c9:
                                return 0x83;
                        case 0x00d1:
                                return 0x84;
                        case 0x00d6:
                                return 0x85;
                        case 0x00dc:
                                return 0x86;
                        case 0x00e1:
                                return 0x87;
                        case 0x00e0:
                                return 0x88;
                        case 0x00e2:
                                return 0x89;
                        case 0x00e4:
                                return 0x8a;
                        case 0x00e3:
                                return 0x8b;
                        case 0x00e5:
                                return 0x8c;
                        case 0x00e7:
                                return 0x8d;
                        case 0x00e9:
                                return 0x8e;
                        case 0x00e8:
                                return 0x8f;
                        case 0x00ea:
                                return 0x90;
                        case 0x00eb:
                                return 0x91;
                        case 0x00ed:
                                return 0x92;
                        case 0x00ec:
                                return 0x93;
                        case 0x00ee:
                                return 0x94;
                        case 0x00ef:
                                return 0x95;
                        case 0x00f1:
                                return 0x96;
                        case 0x00f3:
                                return 0x97;
                        case 0x00f2:
                                return 0x98;
                        case 0x00f4:
                                return 0x99;
                        case 0x00f6:
                                return 0x9a;
                        case 0x00f5:
                                return 0x9b;
                        case 0x00fa:
                                return 0x9c;
                        case 0x00f9:
                                return 0x9d;
                        case 0x00fb:
                                return 0x9e;
                        case 0x00fc:
                                return 0x9f;
                        case 0x2020:
                                return 0xa0;
                        case 0x00b0:
                                return 0xa1;
                        case 0x00a2:
                                return 0xa2;
                        case 0x00a3:
                                return 0xa3;
                        case 0x00a7:
                                return 0xa4;
                        case 0x2022:
                                return 0xa5;
                        case 0x00b6:
                                return 0xa6;
                        case 0x00df:
                                return 0xa7;
                        case 0x00ae:
                                return 0xa8;
                        case 0x00a9:
                                return 0xa9;
                        case 0x2122:
                                return 0xaa;
                        case 0x00b4:
                                return 0xab;
                        case 0x00a8:
                                return 0xac;
                        case 0x2260:
                                return 0xad;
                        case 0x00c6:
                                return 0xae;
                        case 0x00d8:
                                return 0xaf;
                        case 0x221e:
                                return 0xb0;
                        case 0x00b1:
                                return 0xb1;
                        case 0x2264:
                                return 0xb2;
                        case 0x2265:
                                return 0xb3;
                        case 0x00a5:
                                return 0xb4;
                        case 0x00b5:
                                return 0xb5;
                        case 0x2202:
                                return 0xb6;
                        case 0x2211:
                                return 0xb7;
                        case 0x220f:
                                return 0xb8;
                        case 0x03c0:
                                return 0xb9;
                        case 0x222b:
                                return 0xba;
                        case 0x00aa:
                                return 0xbb;
                        case 0x00ba:
                                return 0xbc;
                        case 0x03a9:
                                return 0xbd;
                        case 0x00e6:
                                return 0xbe;
                        case 0x00f8:
                                return 0xbf;
                        case 0x00bf:
                                return 0xc0;
                        case 0x00a1:
                                return 0xc1;
                        case 0x00ac:
                                return 0xc2;
                        case 0x221a:
                                return 0xc3;
                        case 0x0192:
                                return 0xc4;
                        case 0x2248:
                                return 0xc5;
                        case 0x2206:
                                return 0xc6;
                        case 0x00AB:
                                return 0xc7;
                        case 0x00BB:
                                return 0xc8;
                        case 0x2026:
                                return 0xc9;
                        case 0x00A0:
                                return 0xca;
                        case 0x00C0:
                                return 0xcb;
                        case 0x00C3:
                                return 0xcc;
                        case 0x00D5:
                                return 0xcd;
                        case 0x0152:
                                return 0xce;
                        case 0x0153:
                                return 0xcf;
                        case 0x2013:
                                return 0xd0;
                        case 0x2014:
                                return 0xd1;
                        case 0x201c:
                                return 0xd2;
                        case 0x201d:
                                return 0xd3;
                        case 0x2018:
                                return 0xd4;
                        case 0x2019:
                                return 0xd5;
                        case 0x00f7:
                                return 0xd6;
                        case 0x25ca:
                                return 0xd7;
                        case 0x00ff:
                                return 0xd8;
                        case 0x0178:
                                return 0xd9;
                        case 0x2044:
                                return 0xda;
                        case 0x20ac:
                                return 0xdb;
                        case 0x2039:
                                return 0xdc;
                        case 0x203a:
                                return 0xdd;
                        case 0xfb01:
                                return 0xde;
                        case 0xfb02:
                                return 0xdf;
                        case 0x2021:
                                return 0xe0;
                        case 0x00b7:
                                return 0xe1;
                        case 0x201a:
                                return 0xe2;
                        case 0x201e:
                                return 0xe3;
                        case 0x2030:
                                return 0xe4;
                        case 0x00c2:
                                return 0xe5;
                        case 0x00ca:
                                return 0xe6;
                        case 0x00c1:
                                return 0xe7;
                        case 0x00cb:
                                return 0xe8;
                        case 0x00c8:
                                return 0xe9;
                        case 0x00cd:
                                return 0xea;
                        case 0x00ce:
                                return 0xeb;
                        case 0x00cf:
                                return 0xec;
                        case 0x00cc:
                                return 0xed;
                        case 0x00d3:
                                return 0xee;
                        case 0x00d4:
                                return 0xef;
                        case 0xf8ff:
                                return 0xf0;
                        case 0x00d2:
                                return 0xf1;
                        case 0x00da:
                                return 0xf2;
                        case 0x00db:
                                return 0xf3;
                        case 0x00d9:
                                return 0xf4;
                        case 0x0131:
                                return 0xf5;
                        case 0x02c6:
                                return 0xf6;
                        case 0x02dc:
                                return 0xf7;
                        case 0x00af:
                                return 0xf8;
                        case 0x02d8:
                                return 0xf9;
                        case 0x02d9:
                                return 0xfa;
                        case 0x02da:
                                return 0xfb;
                        case 0x00b8:
                                return 0xfc;
                        case 0x02dd:
                                return 0xfd;
                        case 0x02db:
                                return 0xfe;
                        case 0x02c7:
                                return 0xff;
                        default:
                                return 0xffffff3f;
                        }
                }
        };

        algos::apply_copy(codepoints_first, codepoints_last, destination,
                          [&encoding_was_total, encode](uint32_t x) {
                                  uint32_t result = encode(x);
                                  uint8_t macintosh_char = result & 0xff;
                                  if (result != macintosh_char) {
                                          encoding_was_total = false;
                                  }
                                  return macintosh_char;
                          });

        return encoding_was_total;
}
