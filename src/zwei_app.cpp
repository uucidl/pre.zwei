/**
  # MIME format

  ## References:

  ### Message Headers and Bodies

  RFC 5322 -- Internet Message Format
  https://tools.ietf.org/rfc/rfc5322.txt

  RFC 2045 -- MIME part 1: Format of Internet Message Bodies
  https://tools.ietf.org/rfc/rfc2045.txt

  RFC 2046 -- MIME part 2: Media Types
  RFC 2047 -- MIME part 3: Message Header Extensions for Non-ASCII text
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

  TODO(nicolas) collect a list of encountered charsets

  TODO(nicolas) form a simple blacklist that would allow cleaning the
  database. (Charsets for instance?)

  TODO(nicolas) charsets can be deducted from parties in a correspondance,
  possibly by MUA.
*/

#include "zwei_app.hpp"

#include "zwei_inlines.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_logging.hpp"

#include "message_parsers.hpp"

extern "C" EXPORT CHECK_MIME_MESSAGE(check_mime_message)
{
        if (range->error == BR_NoError && range->cursor == range->end) {
                range->next(range);
        }

        struct AdhocParserState adhoc;
        bool continue_adhoc = true;
        parse_message_adhoc_make(&adhoc);

        while (range->error == BR_NoError) {
                if (continue_adhoc) {
                        continue_adhoc =
                            parse_message_adhoc(&adhoc, (char *)range->start,
                                                range->end - range->start);
                }

                range->next(range);
        }

        if (range->error == BR_ReadPastEnd) {
                if (continue_adhoc) {
                        continue_adhoc = parse_message_adhoc(
                            &adhoc, (char *)range->start, 0);
                }
        }

        if (!continue_adhoc) {
                error_print("adhoc failed");
        }

        assert(adhoc.from_count >= 1, "unexpected From count");
        assert(adhoc.sender_count <= 1, "unexpected Sender: count");
        if (adhoc.messageid_count != 1) {
                // NOTE(nicolas) we have actually seen emails with 3x Message-Id
                // tags!
                // Of course only one of them was really valid!
                // TODO(nicolas) reject invalid Message-Id headers
                error_print("surprising messageid count");
        }

        parse_message_adhoc_destroy(&adhoc);
}

extern "C" EXPORT PARSE_ZOE_MAILSTORE_PATH(parse_zoe_mailstore_path)
{
        *result = {};

        if (!cstr_endswith(filename, ".eml")) {
                return -1;
        } else {
                char const *extension = cstr_last_occurrence(filename, '.');
                char const *maildir_flags =
                    cstr_walk_backwards_until(filename, extension, ':');
                char const *uuid_end =
                    maildir_flags > filename ? maildir_flags : extension;
                char const *uuid_start = filename;

                trace_print(uuid_start, uuid_end - uuid_start);

                // parse UUID and date from UUID
                // example: C8E4A7A0014A88C76DA9A00DDF5D464B

                if (uuid_end - uuid_start < 16) {
                        error_print("uuid string too short");
                        return -2;
                } else {
                        uint64_t unix_epoch_millis = 0;
                        uint64_t unique = 0;
                        uint64_t sequence = 0;
                        uint64_t node = 0;

                        assert(2 * NCOUNT(result->uuid) ==
                                   uuid_end - uuid_start,
                               "not enough room in result for uuid");

                        uint8_t *uuid_value = result->uuid;
                        for (auto cursor = uuid_start; cursor < uuid_end;
                             cursor++) {
                                char c = *cursor;
                                bool ok = false;
                                uint8_t nibble = 0;
                                uint8_t fromzero = c - '0';
                                if (fromzero >= 0 && fromzero < 10) {
                                        nibble = fromzero;
                                        ok = true;
                                }
                                uint8_t froma = c - 'a';
                                if (froma >= 0 && froma < 6) {
                                        nibble = 10 + froma;
                                        ok = true;
                                }
                                uint8_t fromA = c - 'A';
                                if (fromA >= 0 && fromA < 6) {
                                        nibble = 10 + fromA;
                                        ok = true;
                                }

                                if (!ok) {
                                        trace_print("weird format");
                                        assert(false, "weird format");
                                        return -2;
                                }

                                auto nibble_index = cursor - uuid_start;
                                auto byte_index = nibble_index / 2;
                                assert(byte_index < 16, "check index");
                                uuid_value[byte_index] |=
                                    nibble << (4 * (1 - (nibble_index & 1)));
                        }

                        struct {
                                uint64_t *value;
                                uint8_t byte_start;
                                uint8_t byte_end;
                                uint8_t byte_power;
                        } segments[] = {
                            {&unix_epoch_millis, 0, 4, 32 - 8},
                            {&unix_epoch_millis, 4, 6, 16 + 32 - 8},
                            {&unique, 6, 10, 32 - 8},
                            {&sequence, 10, 12, 16 - 8},
                            {&node, 12, 16, 32 - 8},
                        };

                        for (auto segment : segments) {
                                uint64_t *value = segment.value;
                                uint8_t byte_power = segment.byte_power;

                                for (auto cursor =
                                         &uuid_value[segment.byte_start];
                                     cursor < &uuid_value[segment.byte_end];
                                     cursor++) {
                                        uint8_t byte = *cursor;
                                        *value |= (uint64_t)byte << byte_power;
                                        byte_power -= 8;
                                }
                        }

                        result->unix_epoch_millis = unix_epoch_millis;
                }
        }

        return 0;
};
