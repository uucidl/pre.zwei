#include "zwei_app.hpp"
#include "zwei_textapp.cpp"

#include <cstdio>

ZWEI_API INIT_APP(init_app);
ZWEI_API GET_MESSAGE_SUMMARY(get_message_summary);
ZWEI_API PARSE_ZOE_MAILSTORE_FILENAME(parse_zoe_mailstore_filename);

int main(int argc, char **argv)
{
        Platform platform = {};          // trivial non-operative platform.
        ProgramResources resources = {}; // no memory whatsoever.

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

                        (void)process_message;         // TODO(nicolas)
                        (void)print_processed_message; // TODO(nicolas)

                        break;
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
