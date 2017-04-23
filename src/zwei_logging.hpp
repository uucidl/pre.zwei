#pragma once

struct TextOutputGroup;

void text_output_group_print(int filedesc, TextOutputGroup const &group);

/** traces and clears the group */
void trace(TextOutputGroup *group);

/** traces and clears the group */
void error(TextOutputGroup *group);

/** print to the user console */
void console(TextOutputGroup* group);

void error_print(char const *message);
void trace_print(char const *message);
