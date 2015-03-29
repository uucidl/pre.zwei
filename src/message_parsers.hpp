struct AdhocParserState
{
        struct MemoryArena field_content_arena;
        // TODO(nicolas) can be made an enum + flags
        bool record_from_on;
        bool record_messageid_on;
        bool record_replyto_on;
        bool record_sender_on;
        bool record_subject_on;
        bool record_to_on;

        bool record_on;
        bool record_truncated;

        // TODO(nicolas) replace by propert transient memory
        char memory[8192];

        int cs;

        int from_count;
        int sender_count;
        int messageid_count;
};

void parse_message_adhoc_make(struct AdhocParserState* state);
void parse_message_adhoc_destroy(struct AdhocParserState* state);
bool parse_message_adhoc(struct AdhocParserState* state, char* block_start, size_t block_size);
