#include "zoe.hpp"

#include "bits.hpp"
#include "hammer_iterators.hpp"
#include "hammer_utils.hpp"
#include "zwei_logging.hpp"

#include "hammer.hpp"

zw_internal struct {
        HParser *uuid_eml_file;
} zoe_support;

HAMMER_ACTION(act_uuid)
{
        using algos::begin;
        using algos::end;

        const auto first = begin(p->ast);
        const auto last = end(p->ast);
        size_t bytes_size = 16;
        uint8_t *bytes = (uint8_t *)h_arena_malloc(p->arena, bytes_size);

        algos::apply_copy(first, last, NibbleBigEndianOutputIterator(bytes),
                          [](const HParsedToken *token) {
                                  // TODO(nicolas): see better version in
                                  // rfc2045.cpp
                                  auto x = H_CAST_UINT(token);
                                  if (x >= 'A' && x <= 'F') {
                                          return 10 + (x - 'A');
                                  }
                                  return x - '0';
                          });

        return H_MAKE_BYTES(bytes, bytes_size);
}

zw_internal void zoe_support_init()
{
#if ZWEI_UNIT_TESTS
        extern void test_bits();
        test_bits();
#endif
        H_RULE(hex_digit, UH_IN("0123456789ABCDEF"));
        H_ARULE(uuid, h_repeat_n(hex_digit, 2 * 16));
        H_RULE(eml_extension, UH_TOKEN(".eml"));

        H_RULE(standard_maildir_flag, UH_IN("PRSTDF"));
        H_RULE(maildir_tags, UH_SEQ(UH_IN(":;"), h_ch('2'), h_ch(','),
                                    h_many1(standard_maildir_flag)));

        H_RULE(uuid_eml_file,
               UH_SEQ(uuid, h_optional(maildir_tags), eml_extension));

        zoe_support.uuid_eml_file = uuid_eml_file;

#if ZWEI_UNIT_TESTS
        trace_print("Testing zoe parsers:");
        CHECK_PARSER(uuid, "66D114D80143B4AF14E0DDD88012BC8B");
        CHECK_PARSER(eml_extension, ".eml");
        CHECK_PARSER(uuid_eml_file, "66D114D80143B4AF14E0DDD88012BC8B.eml");
        CHECK_PARSER(uuid_eml_file, "66D114D80143B4AF14E0DDD88012BC8B:2,T.eml");
        CHECK_PARSER(uuid_eml_file, "66D114D80143B4AF14E0DDD88012BC8B:2,P.eml");
        CHECK_PARSER(uuid_eml_file,
                     "66D114D80143B4AF14E0DDD88012BC8B:2,TP.eml");
#endif
}

zw_internal MayFail<ZoeMailStoreFile>
zoe_parse_uuid_filename(const char *filename, size_t filename_size)
{
        using Result = MayFail<ZoeMailStoreFile>;
        auto parse_result = h_parse(zoe_support.uuid_eml_file,
                                    (uint8_t *)filename, filename_size);
        if (!parse_result) {
                return Result::failure(1);
        }

        ZoeMailStoreFile result = {};

        using algos::source;
        using algos::begin;
        using algos::end;
        using algos::copy_n;

        auto const &uuid_token = source(begin(parse_result->ast));
        zw_assert(uuid_token->token_type == TT_BYTES, "unexpected token");
        copy_n(uuid_token->bytes.token, uuid_token->bytes.len, result.uuid);

        h_parse_result_free(parse_result);

        uint8_t *uuid_value = result.uuid;
        uint64_t unix_epoch_millis = 0;
        uint64_t unique = 0;
        uint64_t sequence = 0;
        uint64_t node = 0;
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
        // split input uuid bytes into the integral values above.
        for (auto segment : segments) {
                uint64_t *value = segment.value;
                uint8_t byte_power = segment.byte_power;

                for (auto cursor = &uuid_value[segment.byte_start];
                     cursor < &uuid_value[segment.byte_end]; cursor++) {
                        uint8_t byte = *cursor;
                        *value |= (uint64_t)byte << byte_power;
                        byte_power -= 8;
                }
        }
        result.unix_epoch_millis = unix_epoch_millis;
        return result;
}
