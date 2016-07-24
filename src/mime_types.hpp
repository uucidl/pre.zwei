#pragma once

// NOTE(nicolas): from
// http://www.iana.org/assignments/character-sets/character-sets.xhtml

enum Charset {
        Charset_UNSUPPORTED = 0,
        Charset_US_ASCII = 3,
        Charset_ISO_8859_1 = 4,
        Charset_UTF_8 = 106,
        Charset_ISO_8859_15 = 111,
};

// IANA MIME TYPES
// @see [[http://www.iana.org/assignments/media-types/media-types.xhtml]]

struct TextMedia {
        Charset charset;
};

struct MultipartMedia {
        ByteCountedRange boundary;
};

struct MediaType {
        enum { IanaType_APPLICATION,
               IanaType_AUDIO,
               IanaType_EXAMPLE,
               IanaType_IMAGE,
               IanaType_MESSAGE,
               IanaType_MODEL,
               IanaType_MULTIPART,
               IanaType_TEXT,
               IanaType_VIDEO,
               IanaType_UNKNOWN,
               IanaType_LAST,
        } iana_type;

        ByteCountedRange type;
        ByteCountedRange subtype;

        union {
                TextMedia text_media;
                MultipartMedia multipart_media;
        };
};
