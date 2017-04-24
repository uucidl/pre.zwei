#include "zwei_app.cpp"

#include "abnf_rfc5234.cpp"
#include "base64.cpp"
#include "hammer_iterators.cpp"
#include "hammer_utils.cpp"
#include "mail_rfc5322_base.cpp"
#include "rfc2045.cpp"
#include "rfc2047.cpp"
#include "rfc5322.cpp"
#include "zoe.cpp"
#include "zwei_iobuffer.cpp"
#include "zwei_text.cpp"
// TODO(uucidl): TAG(platform) logging should be moved to platform layer
#include "zwei_win32_logging.cpp"

#if ZWEI_UNIT_TESTS
#include "test_bits.cpp"
#endif
