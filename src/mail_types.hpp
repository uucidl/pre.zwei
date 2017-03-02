#pragma once

enum ContentTransferEncodingType {
        ContentTransferEncoding_7BIT,
        ContentTransferEncoding_8BIT,
        ContentTransferEncoding_BINARY,
        ContentTransferEncoding_QUOTED_PRINTABLE,
        ContentTransferEncoding_BASE64
};

static inline char const *
ContentTransferEncodingType_string(ContentTransferEncodingType x)
{
        switch (x) {
        case ContentTransferEncoding_7BIT:
                return "ContentTransferEncoding_7BIT";
        case ContentTransferEncoding_8BIT:
                return "ContentTransferEncoding_8BIT";
        case ContentTransferEncoding_BINARY:
                return "ContentTransferEncoding_BINARY";
        case ContentTransferEncoding_QUOTED_PRINTABLE:
                return "ContentTransferEncoding_QUOTED_PRINTABLE";
        case ContentTransferEncoding_BASE64:
                return "ContentTransferEncoding_BASE64";
        }
}
