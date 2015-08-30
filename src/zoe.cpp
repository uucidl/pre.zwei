#include "zoe.hpp"

#include "zwei_logging.hpp"

zw_internal MayFail<ZoeMailStoreFile>
zoe_parse_uuid_filename(const char *filename, size_t filename_size)
{
        using algos::sink;
        MayFail<ZoeMailStoreFile> result;

        // TODO(nicolas): turn into hammer parser

        if (!cstr_endswith(filename, ".eml")) {
                result.errorcode = 1;
                return result;
        } else {
                char const *extension = cstr_last_occurrence(filename, '.');
                char const *maildir_flags =
                    cstr_walk_backwards_until(filename, extension, ':');
                char const *uuid_end =
                    maildir_flags > filename ? maildir_flags : extension;
                char const *uuid_start = filename;

                // parse UUID and date from UUID
                // example: C8E4A7A0014A88C76DA9A00DDF5D464B

                if (uuid_end - uuid_start < 16) {
                        error_print("uuid string too short");
                        result.errorcode = 2;
                        return result;
                } else {
                        uint64_t unix_epoch_millis = 0;
                        uint64_t unique = 0;
                        uint64_t sequence = 0;
                        uint64_t node = 0;

                        ssize_t const max_nibble_count =
                            2 * NCOUNT(sink(result).uuid);
                        if (uuid_end - uuid_start > max_nibble_count) {
                                result.errorcode = 2;
                                return result;
                        }

                        uint8_t *uuid_value = sink(result).uuid;
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

                                if (cursor - uuid_start > max_nibble_count) {
                                        ok = false;
                                }

                                if (!ok) {
                                        result.errorcode = 2;
                                        return result;
                                }

                                auto nibble_index = cursor - uuid_start;
                                auto byte_index = nibble_index / 2;
                                zw_assert(byte_index < 16, "check index");
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

                        // TODO(nicolas): it is probably worth having when
                        // filing a mail
                        sink(result).unix_epoch_millis = unix_epoch_millis;
                }
        }

        return result;
}
