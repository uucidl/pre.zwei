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

  TODO(nicolas) write lenient mail parser inspired by the ABNF one
  targetting the fields we want. Fields which are in 7bit can be
  tokenized. Fields which are 8bit should be stored and tokenized
  later after some further analysis has been made.

  Field names themselves can be interesting as they identify certain
  patterns of a sender.

  NOTES(nicolas): it seems rather frequent for foreign language operators
  to have 8-bit characters in their From: One would assume that they
  then used the charset that was in the Content-Type header.

  Quite a bit of my mails from my Sylpheed period are in that form!

  TODO(nicolas) collect a list of encountered charsets

  TODO(nicolas) form a simple blacklist that would allow cleaning the
  database. (Charsets for instance?)

  TODO(nicolas) charsets can be deducted from parties in a correspondance,
  possibly by MUA.

  TODO(nicolas) support alternative 8-bit text sequences for bodies + from +
  subject and test against corpus

  TODO(nicolas) oh my, dates too can have 8-bit crap in them

  TODO(nicolas) recompile and reload dynamically the parser so as to resume a
  chunk of work

  TODO(nicolas) a lenient parser would just ignore the text of any field it
  doesn't care about and allow 8-bit there

  TODO(nicolas) revert and split lenient and strict parsers. Strict parsers to
  be used for sending only or for testing.
*/

#include "zwei_mime.hpp"

#include "zwei.hpp"
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
