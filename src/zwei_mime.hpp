#pragma once

struct BufferRange;

#define CHECK_MIME_MESSAGE(name) void name(struct BufferRange* range)
typedef CHECK_MIME_MESSAGE(CheckMimeMessageFn);
