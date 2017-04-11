#pragma once

struct TextOutputGroup;

// TODO(nicolas): traces should feature a timestamp for
// performance and auditing. i.e. the platform layer should
// offer a logging service.

void text_output_group_print(int filedesc, TextOutputGroup const &group);

/** traces and clears the group */
void trace(TextOutputGroup *group);

/** traces and clears the group */
void error(TextOutputGroup *group);

void error_print(char const *message);
void trace_print(char const *message);
void trace_print_n(char const *message, size_t message_len);
