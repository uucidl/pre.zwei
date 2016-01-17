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

#include "../secure_hash_standard.hpp"
#include "../zwei_files.hpp"

#include "../../modules/uu.spdr/include/spdr/spdr.hh"

#include "../algos.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <dispatch/dispatch.h>

#include <cassert>
#include <cstdlib>
#include <limits>

struct FileShaWork {
        FileLoader *file_loader;
        FileLoaderHandle file_handle;
        uint8_t digest[20];
};

zw_global SPDR_Context *global_spdr = nullptr;

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

void print_result_line(FileShaWork const &task)
{
        for (size_t byteIndex = 0; byteIndex < countof(task.digest);
             byteIndex++) {
                uint8_t const byte = task.digest[byteIndex];
                printf("%c%c", byte_to_hex[byte >> 4], byte_to_hex[byte & 0xF]);
        }

        printf("  %s\n", get_filepath(*task.file_loader, task.file_handle));
}

int main(int argc, char *argv[])
{
#if SHASUM_TRACING
        zw_assert(
            0 == spdr_init(&global_spdr, malloc(MEGABYTES(96)), MEGABYTES(96)),
            "spdr init");
        spdr_enable_trace(global_spdr, 1);
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

        while (count_pending_files(file_loader) || tasks_count > 0 ||
               !algos::empty(available_files(file_loader))) {
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

#include "../secure_hash_standard.cpp"

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
