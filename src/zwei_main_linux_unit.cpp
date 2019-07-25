#include "zwei_app.hpp"
#include "zwei_files.hpp"
#include "zwei_textapp.cpp"

#include <cstdio>

ZWEI_API INIT_APP(init_app);
ZWEI_API GET_MESSAGE_SUMMARY(get_message_summary);
ZWEI_API PARSE_ZOE_MAILSTORE_FILENAME(parse_zoe_mailstore_filename);

int main(int argc, char **argv)
{
        Platform platform = {};          // trivial non-operative platform.
        ProgramResources resources = {}; // no memory whatsoever.

        const auto scratch_buffer_size = 1 * 1024 * 1024 * 1024;
        const auto scratch_buffer = calloc(scratch_buffer_size, 1);
        fatal_ifnot(scratch_buffer, "Could not allocate scratch buffer");
        DEFER(free(scratch_buffer));

        init_app(platform, 0, &resources);

        Zwei zwei = {
            get_message_summary,
            parse_zoe_mailstore_filename,
        };

        /* parse args */ {
                int argi = 1;
                while (argi < argc) {
                        const auto filename = argv[argi];
                        argi++;
                        if (should_skip_message_file(zwei, filename, filename,
                                                     0, UINT64_MAX)) {
                                continue;
                        }

                        MemoryArena transient_arena =
                            memory_arena(scratch_buffer, scratch_buffer_size);
                        MemoryArena result_arena =
                            push_sub_arena(transient_arena, MEGABYTES(2));

                        const auto file_loader_size =
                            get_file_loader_allocation_size(1, 1 * 1024 * 1024);
                        auto &file_loader = create_file_loader(
                            1, push_bytes(&transient_arena, file_loader_size),
                            file_loader_size);
                        push_file(file_loader, filename, 0);
                        wait_for_available_files(file_loader);

                        for (auto &&fh : available_files(file_loader)) {
                                accept(file_loader, fh);
                                printf("Loaded file %s\n",
                                       get_filename(file_loader, fh));
                                ProcessedMessage result = {};
                                const auto content =
                                    get_content(file_loader, fh);
                                DEFER(release_content(file_loader, fh));
                                process_message(
                                    zwei, get_filename(file_loader, fh),
                                    get_filepath(file_loader, fh),
                                    begin(content), end(content),
                                    transient_arena, result_arena, result);
                                print_processed_message(result,
                                                        transient_arena);
                        }

                        destroy(file_loader);
                }
                if (argi < argc) {
                        fprintf(stderr, "Ignoring extra arguments: %s...\n",
                                argv[argi]);
                }
        }

        return 0;
}

#include "zwei_lib_osx_unit.cpp"

// TODO(nicolas): because we're using h_parser instead of h_parser_mm,
// hammer allocates parsers using the system allocator, and since
// we're not doing anything special to release them, a leak sanitizer
// will basically detect all parsers as having been leaked.

#define ZWEI_TEXTAPP_IMPLEMENTATION
#include "secure_hash_standard.cpp"
#include "zwei_files_unix_synchronous.cpp"
#include "zwei_textapp.cpp"
