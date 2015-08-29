/**
   adhoc fisrt-level parser for MIME messages.

   Validated on a corpus with 100k> mails which contains lots of odd emails.

   FEATURE(nicolas): print content of From: Sender: Message-Id: and In-Reply-To:
 */

%%{
        machine adhoc_parser;

        action start_from {
                state->record_from_on = true;
        }

        action start_sender {
                state->record_sender_on = true;
        }

        action start_messageid {
                state->record_messageid_on = true;
        }

        action start_replyto {
                state->record_replyto_on = true;
        }

        action start_subject {
                state->record_subject_on = true;
        }

        action start_to {
                state->record_to_on = true;
        }

        action record {
                record_char();
        }

        action recorded {
                if (state->record_on) {
                        // TODO(nicolas) collapse everything into one flag
                        if (state->record_from_on) {
                                state->from_count++;
                                state->record_from_on = false;
                                trace_print("FROM");
                        }

                        if (state->record_sender_on) {
                                state->sender_count++;
                                state->record_sender_on = false;
                                trace_print("SENDER");
                        }

                        if (state->record_messageid_on) {
                                state->messageid_count++;
                                state->record_messageid_on = false;
                                trace_print("MESSAGE-ID");
                        }

                        if (state->record_replyto_on) {
                                state->record_replyto_on = false;
                                trace_print("IN-REPLY-TO");
                        }

                        if (state->record_subject_on) {
                                state->record_subject_on = false;
                                trace_print("SUBJECT");
                        }

                        if (state->record_to_on) {
                                state->record_to_on = false;
                                trace_print("TO");
                        }

                        assert(state->field_content_arena.base[0] != ' ', "did not expect whitespace");
                        assert(state->field_content_arena.base[0] != '\t', "did not expect whitespace");

                        trace_print_n((char*)state->field_content_arena.base, state->field_content_arena.used);
                        state->record_on = false;
                        reset_arena();
                }
        }

        action recordeds {
        }

        action body_start {}

        action body_end {}

        action field_char {
                state->record_on = true;
        }

        CR = "\r";
        LF = "\n";
        CRLF = CR LF;

        whitespace = ' ' | '\t';

        field_prefix = (0x21..0x39 | 0x3b..0x7e)+ ":";
        line_content = (any - CRLF)+;
        field_lines = line_content :> (CRLF
                    (line_content - field_prefix)
                )*;

        captured_field =
                ("From:"i %start_from|
                 "Sender:"i %start_sender|
                 "Subject:"i %start_subject|
                 "In-Reply-To:"i %start_replyto|
                 "Message-Id:"i %start_messageid|
                 "To:"i %start_to) (whitespace+ CRLF?)* <: (field_lines >field_char $record %recorded) CRLF;

        field = captured_field | (field_prefix field_lines CRLF);

        message = field+ :> (CRLF @body_start (any+ %body_end))?;
}%%

%%{
  main := message;
}%%

%% write data;

void parse_message_adhoc_make(struct AdhocParserState* state)
{
        state->field_content_arena = memory_arena(state->memory, sizeof state->memory);
        state->record_from_on = false;
        state->record_sender_on = false;
        state->record_messageid_on = false;
        state->record_replyto_on = false;
        state->record_subject_on = false;

        state->record_on = false;
        state->record_truncated = false;

        state->from_count = 0;
        state->sender_count = 0;
        state->messageid_count = 0;

        %% access state->;
        %% write init;
}

void parse_message_adhoc_destroy(struct AdhocParserState* state)
{
        // no op
}

bool parse_message_adhoc(struct AdhocParserState* state, char* block_start, size_t size)
{
        MayFail<bool> result;

        char *p = block_start;
        char *pe = block_start + size;
        char *eof = size == 0 ? pe : nullptr;

        auto reset_arena = [state]() {
                state->field_content_arena.used = 0;
        };

        auto record_char = [state,&p]() {
                if(state->record_on) {
                        if (state->field_content_arena.used < state->field_content_arena.size) {
                                *((char*)push_bytes(&state->field_content_arena, 1)) = *p;
                        } else {
                                state->record_truncated = true;
                        }
                }
        };

        %% access state->;
        %% write exec;
        if (p < pe && state->cs < %%{ write first_final; }%%) {
                assert(false, "debug this");
                return false;
        }
        if (p < pe) {
                assert(false, "debug this");
        }

        return true;
}
