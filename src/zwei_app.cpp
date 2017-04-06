/**

  # Main project todo list

  ## Source Connectors
  POP, IMAP, Twitter, Facebook etc...

  ## Input Parsing
  TODO(nicolas): Parse First Line Of Content (RFC5322) ; for now we have a janky
  extraction of the first characters of the body.

  ## Database
  - Timeline indexing/querying

  ## User Interface
  ## Platform
  TODO(nicolas): work task queue
  TODO(nicolas): LINUX port. (started)
  TODO(nicolas): WIN32 port. (started)
  TODO(nicolas): cross compilation to <powerpc e500 v2> (Synology NAS)

  ## Features

  TODO(nicolas): keep track of when a mail was deleted. Immediately w/o
  opening suggests spam. After a while can be a mail that contains ephemereal
  content.

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
#if defined(ZWEI_UNIT_TESTS)
#include "algos_tests.hpp"
#endif
#include "abnf_rfc5234.hpp"
#include "base64.hpp"
#include "rfc2045.hpp"
#include "rfc2047.hpp"
#include "rfc5322.hpp"
#include "zoe.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_types.hpp"

// modules:
#include "utf8_decode.cpp"
#include "uu_macroman.cpp"

#include <cstddef>    // for offsetof
#include <functional> // for std::cref (TODO(nicolas): can I write it myself?)

struct MacRomanWorkaround {
        // TODO(nicolas) this is way too low
        enum { RUNE_COUNT = 64,
        };
        uint8_t utf8_output_block[3 * RUNE_COUNT];
        size_t utf8_output_block_count;

        uint32_t utf8decoder_state;
        uint32_t utf8decoder_codepoint;

        uint32_t ucs4_codepoints_block[RUNE_COUNT];
        uint8_t macintosh_chars[RUNE_COUNT];
        uint32_t macintosh_chars_utf8decoder_state;
        uint32_t macintosh_chars_utf8decoder_codepoint;
};

zw_global RFC5322 mail_parsers;
zw_global bool global_debug_mode;
zw_global Platform global_platform;
zw_global SPDR_Context *global_spdr;

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
                        return Utf8DecodeResult_Accept ==
                               utf8_decode(&state->utf8decoder_state,
                                           &state->utf8decoder_codepoint, x);
                };
                auto decode_result = algos::apply_copy_bounded_if(
                    block_first, block_last, codepoint_last,
                    codepoint_buffer_last,
                    [state](const uint8_t) {
                            return state->utf8decoder_codepoint;
                    },
                    decode_codepoint);

                if (Utf8DecodeResult_Accept != state->utf8decoder_state) {
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
            Utf8DecodeResult_Accept == state->macintosh_chars_utf8decoder_state;

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
zw_internal IOBufferIterator
macroman_workaround_stream(IOBufferIterator *input, struct MemoryArena *arena)
{
        struct Stream {
                IOBufferIterator *input;
                MacRomanWorkaround state;
        } *stream = push_pointer_rvalue(arena, stream);

        stream->input = input;
        stream->state.utf8decoder_state = Utf8DecodeResult_Accept;
        stream->state.macintosh_chars_utf8decoder_state =
            Utf8DecodeResult_Accept;

        auto next = [](IOBufferIterator *iobuffer) {
                struct Stream *stream = reinterpret_cast<struct Stream *>(
                    iobuffer->start -
                    offsetof(struct Stream, state.utf8_output_block));
                IOBufferIterator &input_iobuffer = *stream->input;
                if (input_iobuffer.cursor >= input_iobuffer.end) {
                        if (IOBufferIteratorError_NoError !=
                            refill_iobuffer(&input_iobuffer)) {
                                return fail(iobuffer, input_iobuffer.error);
                        }
                }

                auto input_cursor = macroman_workaround_block(
                    &stream->state, input_iobuffer.cursor, input_iobuffer.end);
                if (!input_cursor) {
                        return fail(iobuffer,
                                    IOBufferIteratorError_DecodingError);
                }

                input_iobuffer.cursor = input_cursor;

                iobuffer->start = &stream->state.utf8_output_block[0];
                iobuffer->end =
                    iobuffer->start + stream->state.utf8_output_block_count;
                iobuffer->cursor = iobuffer->start;

                return IOBufferIteratorError_NoError;
        };

        IOBufferIterator iobuffer = {
            &stream->state.utf8_output_block[0],
            &stream->state.utf8_output_block[0],
            &stream->state.utf8_output_block[0],
            IOBufferIteratorError_NoError,
            next,
        };

        refill_iobuffer(&iobuffer);
        return iobuffer;
}

struct MessageBeingParsed {
        enum { UNPARSED,
               MESSAGE_SUMMARY,
        } content_state;
        struct MessageSummary message_summary;
};

ZWEI_API INIT_APP(init_app)
{
        trace_print("Initializing app");
        global_platform = platform;

        global_spdr = global_platform.spdr;

        if (flags & ZWEI_DEBUG_MODE_FLAG) {
                global_debug_mode = true;
        }

#if ZWEI_UNIT_TESTS
        trace_print("Testing algos");
        algos::run_tests();
        trace_print("Testing algos: done");
#endif
        extern void hammer_init();

        hammer_init();
        auto &abnf = make_abnf_rfc5234();
        auto &base64 = make_base64(abnf);
        auto &rfc5322_base = make_rfc5322_base(abnf);
        auto &rfc2045 = make_rfc2045(abnf, rfc5322_base);
        auto &rfc2047 = make_rfc2047(abnf, rfc2045, base64);
        mail_parsers = make_rfc5322(abnf, rfc5322_base, rfc2047, rfc2045);
        zoe_support_init();
}

zw_internal void fill_message_summary(MessageSummary *message_summary,
                                      const HParsedToken *ast,
                                      MemoryArena *arena)
{
        *message_summary = {};

        auto collect = [&message_summary, arena](HParsedToken const *token) {
                enum { NONE,
                       BYTES,
                       BYTES_LIST,
                       MAILBOX,
                       MAILBOX_LIST,
                       CIVIL_DATE_TIME,
                } process = NONE;
                ByteCountedRange *d_bytes_range = nullptr;
                ByteCountedRange **d_bytes_ranges = nullptr;
                size_t *d_bytes_ranges_count = nullptr;
                RawMailbox *d_mailbox = nullptr;
                RawMailbox **d_mailboxes = nullptr;
                size_t *d_mailboxes_count = nullptr;
                CivilDateTime *d_civil_date_time = nullptr;

                if (RFC5322TokenIs(token, MESSAGE_ID_FIELD)) {
                        process = BYTES;
                        d_bytes_range = &message_summary->message_id_bytes;
                } else if (RFC5322TokenIs(token, SUBJECT_FIELD)) {
                        process = BYTES;
                        d_bytes_range = &message_summary->subject_field_bytes;
                } else if (RFC5322TokenIs(token, FROM_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &message_summary->from_mailboxes;
                        d_mailboxes_count =
                            &message_summary->from_mailboxes_count;
                } else if (RFC5322TokenIs(token, TO_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &message_summary->to_mailboxes;
                        d_mailboxes_count =
                            &message_summary->to_mailboxes_count;
                } else if (RFC5322TokenIs(token, CC_FIELD)) {
                        process = MAILBOX_LIST;
                        d_mailboxes = &message_summary->cc_mailboxes;
                        d_mailboxes_count =
                            &message_summary->cc_mailboxes_count;
                } else if (RFC5322TokenIs(token, SENDER_FIELD)) {
                        process = MAILBOX;
                        d_mailbox = &message_summary->sender_mailbox;
                } else if (RFC5322TokenIs(token, IN_REPLY_TO_FIELD)) {
                        process = BYTES_LIST;
                        d_bytes_ranges = &message_summary->in_reply_to_msg_ids;
                        d_bytes_ranges_count =
                            &message_summary->in_reply_to_msg_ids_count;
                } else if (RFC5322TokenIs(token, ORIG_DATE_FIELD)) {
                        process = CIVIL_DATE_TIME;
                        d_civil_date_time = &message_summary->orig_date;
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
                case CIVIL_DATE_TIME: {
                        zw_assert(d_civil_date_time, "d_civil_date_time");
                        zw_assert(d_civil_date_time !=
                                      rfc5322_field_copy_date_time(
                                          token, d_civil_date_time),
                                  "missing");
                } break;
                case NONE:
                        break;
                }
        };
        algos::traverse_each(rfc5322_top(ast), std::cref(collect));
}

ZWEI_API GET_MESSAGE_SUMMARY(get_message_summary)
{
        uint8_t const *full_message = data_first;
        uint8_t const *full_message_end = data_last;
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

                IOBufferIterator message_iobuffer_memory;
                IOBufferIterator *message_iobuffer = &message_iobuffer_memory;
                stream_on_memory(message_iobuffer,
                                 const_cast<uint8_t *>(full_message),
                                 full_message_end - full_message);

                /* macroman decoder setup */
                IOBufferIterator *message_decoder =
                    push_pointer_rvalue(message_arena, message_decoder);
                *message_decoder =
                    macroman_workaround_stream(message_iobuffer, message_arena);

                // @id cfec80a4708df2e90e45023f0d87af7f4eb54a46
                uint8_t *decoded_message =
                    (uint8_t *)push_bytes(message_arena, 0);
                uint8_t *decoded_message_end = decoded_message;

                while (message_iobuffer->error ==
                       IOBufferIteratorError_NoError) {
                        zw_assert(decoded_message_end ==
                                      push_bytes(message_arena, 0),
                                  "non contiguous");
                        decoded_message_end = algos::copy(
                            message_iobuffer->start, message_iobuffer->end,
                            (uint8_t *)push_bytes(message_arena,
                                                  message_iobuffer->end -
                                                      message_iobuffer->start));
                        refill_iobuffer(message_iobuffer);
                }

                if (message_iobuffer->error !=
                    IOBufferIteratorError_ReadPastEnd) {
                        error_print("unknown I/O error while parsing message");
                        return 1;
                }

                full_message = decoded_message;
                full_message_end = decoded_message_end;
        }

        struct MessageBeingParsed message_parsed = {
            MessageBeingParsed::UNPARSED, {},
        };
        // Hammer parsing
        {
                // NOTE(nicolas): we can use the "fields" parser if we
                // want to be less strict.
                auto result = h_parse(mail_parsers.fields, full_message,
                                      full_message_end - full_message);
                if (!result) {
                        trace_print("PARSE ERROR");
                        return 2;
                }
                DEFER(h_parse_result_free(result));

                if (must_print_ast) {
                        rfc5322_print_ast(stdout, result->ast, 0, 4);
                }
                fill_message_summary(&message_parsed.message_summary,
                                     result->ast, result_arena);
                message_parsed.message_summary.valid_rfc5322 =
                    rfc5322_validate(result->ast, *message_arena);
                message_parsed.content_state =
                    MessageBeingParsed::MESSAGE_SUMMARY;

                // Extract first line of readable text
                //
                // TODO(nicolas): when the content is not valid rfc5322,
                // then we might actually be in the middle of the header still.
                // We should skip it using an heuristic.
                //
                // TODO(nicolas): support quoted/encoded content
                // TODO(nicolas): support multipart
                auto body_first = full_message + result->bit_length / 8;
                body_first =
                    algos::find_if(body_first, full_message_end, [](uint8_t x) {
                            if (x == 0x20 || x == 0x09 || x == 0x0d ||
                                x == 0x0a) {
                                    return false;
                            }
                            return true;
                    });

                message_parsed.message_summary.content_transfer_encoding =
                    rfc5322_get_content_transfer_encoding(result->ast);

                // TODO(nicolas): fill up content-type for the body
                // (type + charset for text types)

                size_t first_line_max_count = 200;
                uint8_t *first_line = push_array_rvalue(
                    result_arena, first_line, first_line_max_count);
                auto copy_end = algos::copy_bounded(
                    body_first, full_message_end, first_line,
                    first_line + first_line_max_count);
                message_parsed.message_summary.first_line_bytes.first =
                    first_line;
                message_parsed.message_summary.first_line_bytes.count =
                    copy_end.second - first_line;
        }

        if (message_parsed.content_state ==
            MessageBeingParsed::MESSAGE_SUMMARY) {
                auto raw = message_parsed.message_summary;
                *message_summary = std::move(raw);
                return 0;
        }

        return 3;
}

ZWEI_API PARSE_ZOE_MAILSTORE_FILENAME(parse_zoe_mailstore_filename)
{
        auto zr = zoe_parse_uuid_filename(filename, cstr_len(filename), result);
        if (zr != 0) {
                return -1;
        }
        return 0;
};

#define UU_MACROMAN_IMPLEMENTATION
#include "uu_macroman.cpp"

#define UTF8_DECODE_IMPLEMENTATION
#include "utf8_decode.cpp"

#if ZWEI_UNIT_TESTS
#include "algos_tests.cpp"
#endif
