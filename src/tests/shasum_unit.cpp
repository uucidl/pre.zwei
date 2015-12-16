/*

  "Trying To Do Better Than Perl"

  Compare benchmark this against the OS' shasum. Compare async and
  synchronous versions.

  Note that for this program, the processing (sha1) is much faster
  than the actual I/O.

  It is not an equivalent of an app that has complex processing to do
  per byte. (i.e zwei itself)
 */

#include "../zwei_inlines.hpp"
#include "../zwei_debug.hpp"
#include "../zwei_files.hpp"

#include "../../modules/uu.spdr/include/spdr/spdr.hh"

#include "../algos.hpp"

#include <dispatch/dispatch.h>

#include <cassert>
#include <cstdlib>
#include <limits>

struct FileShaWork {
        FileLoader *file_loader;
        FileLoaderHandle file_handle;
        uint8_t digest[20];
};

zw_internal void
sha1(uint8_t const *bytes, size_t bytes_size, uint8_t result[20]);

void shasum_task(void *tasks_ptr, size_t task_index)
{
        FileShaWork *tasks = (FileShaWork *)tasks_ptr;
        auto &task = tasks[task_index];
        auto &file_loader = *task.file_loader;
        auto fh = task.file_handle;

        SPDR_SCOPE1(global_spdr, "main", "shasum_task",
                    SPDR_INT("fileindex", get_tag(file_loader, fh)));
        auto content = get_content(file_loader, fh);
        auto size = end(content) - begin(content);
        sha1(begin(content), size, task.digest);
        release_content(file_loader, fh);
}

static char byte_to_hex[] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
};

void print_digest_hex(uint8_t const digest[20])
{
        for (int byteIndex = 0; byteIndex < 20; byteIndex++) {
                uint8_t const byte = digest[byteIndex];
                printf("%c%c", byte_to_hex[byte >> 4], byte_to_hex[byte & 0xF]);
        }
}

void print_result_line(FileShaWork const &task)
{
        print_digest_hex(task.digest);
        printf("  %s\n", get_filepath(*task.file_loader, task.file_handle));
}

SPDR_Context *global_spdr = nullptr;

#if defined(ZWEI_INTERNAL)
zw_internal void test_sha1();
#endif

int main(int argc, char *argv[])
{
#if SHASUM_TRACING
        zw_assert(
            0 == spdr_init(&global_spdr, malloc(MEGABYTES(96)), MEGABYTES(96)),
            "spdr init");
        spdr_enable_trace(global_spdr, 1);
#endif
#if defined(ZWEI_INTERNAL)
        test_sha1();
#endif

        SPDR_METADATA1(global_spdr, "thread_name", SPDR_STR("name", "main"));
        SPDR_BEGIN(global_spdr, "main", "main");

        auto arg = argv;
        --argc;
        ++arg;

        auto file_loader_size =
            get_file_loader_allocation_size(argc, GIGABYTES(1));

        FileLoader &file_loader = create_file_loader(
            argc, malloc(file_loader_size), file_loader_size);

        int fileindex = 0;
        while (argc) {
                push_file(file_loader, *arg, fileindex++);
                ++arg;
                --argc;
        }

        dispatch_queue_t work_queue =
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
        size_t tasks_capacity = 8;
        FileShaWork tasks[tasks_capacity];
        size_t tasks_count = 0;

        while (count_pending_files(file_loader) || tasks_count > 0) {
                // TODO(nicolas): might be nice to ask for up to n
                // files so as to benefit from the multithreading for
                // the compute task.
                wait_for_available_files(file_loader);
                auto prev_tasks_count = tasks_count;
                for (auto file_handle : available_files(file_loader)) {
                        if (tasks_count == tasks_capacity) {
                                break;
                        }
                        accept(file_loader, file_handle);
                        auto &task = tasks[tasks_count++];
                        task.file_loader = &file_loader;
                        task.file_handle = file_handle;
                }
                if (prev_tasks_count == tasks_count) {
                        dispatch_apply_f(tasks_count, work_queue, tasks,
                                         shasum_task);
                        algos::for_each_n(tasks, tasks_count,
                                          print_result_line);
                        tasks_count = 0;
                }
        }

        zw_assert(algos::empty(available_files(file_loader)),
                  "must read everything");

        for (auto fh : load_error_files(file_loader)) {
                fprintf(stderr, "could not process %s\n",
                        get_filepath(file_loader, fh));
        }

        destroy(file_loader);

        SPDR_END(global_spdr, "main", "main");
#if SHASUM_TRACING
        char trace_file[4096];
        char *trace_file_end = trace_file;
        cstr_cat(trace_file_end, algos::end(trace_file), argv[0],
                 cstr_find_last(argv[0], '/'));
        cstr_cat(trace_file_end, algos::end(trace_file), "/");
        cstr_cat(trace_file_end, algos::end(trace_file), "shasum_trace.json");
        if (cstr_terminate(trace_file_end, algos::end(trace_file))) {
                printf("Writing traces to %s\n", trace_file);
                int fd = open(trace_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
                spdr_report(global_spdr, SPDR_CHROME_REPORT,
                            [](char const *string, void *user_data) {
                                    int fd = *((int *)user_data);
                                    write(fd, string, cstr_len(string));
                            },
                            &fd);
                close(fd);
        }
        spdr_deinit(&global_spdr);
#endif

        return (0);
}

#if defined(PLATFORM_OSX)
// <OSX implementation for sha1..
#include <CommonCrypto/CommonDigest.h>
zw_internal void
sha1(uint8_t const *bytes, size_t bytes_size, uint8_t result[20])
{
        assert(bytes_size <= std::numeric_limits<CC_LONG>::max());
        CC_SHA1(bytes, static_cast<CC_LONG>(bytes_size), result);
}
// ..OSX implementation for sha1>
#else
zw_internal void
sha1(uint8_t const *bytes, size_t const bytes_size, uint8_t result[20])
{
        if (bytes_size >= 1LL << (64 - 8)) {
                // unsupported message length
                result = {};
                return;
        }

        enum { BLOCK_SIZE = 512 / 8,
        };

        uint32_t K[4] = {
            0x5a827999UL, 0x6ed9eba1UL, 0x8f1bbcdcUL, 0xca62c1d6UL,
        };
        uint32_t h0 = 0x67452301UL;
        uint32_t h1 = 0xefcdab89UL;
        uint32_t h2 = 0x98badcfeUL;
        uint32_t h3 = 0x10325476UL;
        uint32_t h4 = 0xc3d2e1f0UL;

        uint64_t message_length = 8 * bytes_size;
        uint32_t a, b, c, d, e;

        uint8_t padded_block[BLOCK_SIZE];
        size_t byte_index = 0;
        size_t leftover_byte_size =
            (BLOCK_SIZE - (bytes_size % BLOCK_SIZE)) % BLOCK_SIZE;
        size_t padded_byte_size = bytes_size + leftover_byte_size +
                                  (leftover_byte_size < 8 ? BLOCK_SIZE : 0);

#define byte(word, byte_index) (((word) >> (8 * byte_index)) & 0xff)
        // process blocks
        while (byte_index < padded_byte_size) {
                size_t block_end_index = byte_index + BLOCK_SIZE;
                if (block_end_index > bytes_size) {
                        size_t bytes_end_in_block = bytes_size - byte_index;
                        size_t padding_start = bytes_end_in_block;
                        if (bytes_size + 1 >= byte_index &&
                            bytes_size + 1 < block_end_index) {
                                memcpy(padded_block, bytes, bytes_end_in_block);
                                padded_block[padding_start] = 0x80;
                                ++padding_start;
                        }

                        if (BLOCK_SIZE - padding_start < 8) {
                                size_t zero_count = BLOCK_SIZE - padding_start;
                                memset(padded_block + padding_start, 0,
                                       zero_count);
                                padding_start += zero_count;
                        } else {
                                size_t zero_count =
                                    BLOCK_SIZE - 8 - padding_start;
                                memset(padded_block + padding_start, 0,
                                       zero_count);
                                padding_start += zero_count;
                                uint8_t *dest = padded_block + padding_start;
                                size_t byte_index = 8;
                                while (byte_index) {
                                        --byte_index;
                                        *dest = (message_length >>
                                                 (8 * byte_index)) &
                                                0xff;
                                        ++dest;
                                }
                        }
                        bytes = padded_block;
                }
                // preparing message schedule
                uint32_t w[80];
#define word(bytes)                                                            \
        (bytes[3] | (bytes[2] << 8) | (bytes[1] << 16) | (bytes[0] << 24))
                for (int i = 0; i < 16; i++) {
                        w[i] = word(bytes);
                        bytes += 4;
                        byte_index += 4;
                }
#define rotl32_n(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define rotl32_1(x) rotl32_n((x), 1)
                for (int i = 16; i < 80; i++) {
                        w[i] = rotl32_1(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^
                                        w[i - 16]);
                }
#undef word
                a = h0;
                b = h1;
                c = h2;
                d = h3;
                e = h4;
                for (int i = 0; i < 80; i++) {
                        int segment = i / 20;
                        // function calculation
                        uint32_t y = 0;
                        switch (segment) {
                        case 0: {
                                y = (b & c) ^ (~b & d);
                        } break;
                        case 1: {
                                y = b ^ c ^ d;
                        } break;
                        case 2: {
                                y = (b & c) ^ (b & d) ^ (c & d);
                        } break;
                        case 3: {
                                y = b ^ c ^ d;
                        } break;
                        }
                        // combination step
                        uint32_t temp =
                            rotl32_n(a, 5) + y + e + K[segment] + w[i];
                        e = d;
                        d = c;
                        c = rotl32_n(b, 30);
                        b = a;
                        a = temp;
                }

                h0 = a + h0;
                h1 = b + h1;
                h2 = c + h2;
                h3 = d + h3;
                h4 = e + h4;
#undef rotl32_1
#undef rotl32_n
        }

        uint32_t hashes[5] = {h0, h1, h2, h3, h4};
        size_t hashes_count = 5;
        uint32_t *first_hash = hashes;
        while (hashes_count) {
                result[0] = byte(*first_hash, 3);
                result[1] = byte(*first_hash, 2);
                result[2] = byte(*first_hash, 1);
                result[3] = byte(*first_hash, 0);
                result += 4;
                ++first_hash;
                --hashes_count;
        }
#undef byte
}
#endif

#if defined(ZWEI_INTERNAL)
zw_internal void test_sha1()
{
        uint8_t test_digest[20];
        uint8_t test_message[512] = {}; // 512 zeros
        sha1(test_message, 512, test_digest);
        print_digest_hex(test_digest);
        assert(0 == memcmp(test_digest,
                           "5c3eb80066420002bc3dcc7ca4ab6efad7ed4ae5", 20));

        uint8_t test_abc[] = "abc";
        sha1(test_abc, 3, test_digest);
        print_digest_hex(test_digest);
        assert(0 == memcmp(test_abc, "a9993e364706816aba3e25717850c26c9cd0d89d",
                           20));
}
#endif

#if SHASUM_ASYNC
#include "../zwei_files_osx_async.cpp"
#else
#include "../zwei_files_osx_synchronous.cpp"
#endif

extern "C" {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "../../modules/uu.spdr/src/spdr_osx_unit.c"
#pragma clang diagnostic pop
}

/**
 * Local Variables:
 * compile-command: "clang++ \
 *     -DSHASUM_ASYNC=1 -DSHASUM_TRACING=1 -DZWEI_SLOW \
 *     -g -std=c++11 -Wall -Wextra \
 * shasum_unit.cpp -o shasum"
 * End:
 */
