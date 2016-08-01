#if !defined(UU_MACROMAN_PROTOTYPES)
#define UU_MACROMAN_PROTOTYPES

/**

   Encode ucs4 codepoints into "Mac OS Roman" 8-bit encoding.
   characters outside of the encodable range are substituted to by
   '?' (63) if any character had to be substituted, will return false
   to indicate an incomplete encoding.

 */
zw_internal bool ucs4_to_macintosh(uint32_t const *codepoints_first,
                                   uint32_t const *codepoints_last,
                                   uint8_t *destination);
#endif

#if defined(UU_MACROMAN_EMIT_IMPL)
#undef UU_MACROMAN_EMIT_IMPL

#include "algos.hpp"

zw_internal bool ucs4_to_macintosh(uint32_t const *codepoints_first,
                                   uint32_t const *codepoints_last,
                                   uint8_t *destination)
{
        bool encoding_was_total = true;

        auto encode = [](uint32_t codepoint) -> uint32_t {
                if (codepoint < 0x80) {
                        return codepoint & 0xff;
                } else {
                        switch (codepoint) {
                        case 0x00c4:
                                return 0x80;
                        case 0x00c5:
                                return 0x81;
                        case 0x00c7:
                                return 0x82;
                        case 0x00c9:
                                return 0x83;
                        case 0x00d1:
                                return 0x84;
                        case 0x00d6:
                                return 0x85;
                        case 0x00dc:
                                return 0x86;
                        case 0x00e1:
                                return 0x87;
                        case 0x00e0:
                                return 0x88;
                        case 0x00e2:
                                return 0x89;
                        case 0x00e4:
                                return 0x8a;
                        case 0x00e3:
                                return 0x8b;
                        case 0x00e5:
                                return 0x8c;
                        case 0x00e7:
                                return 0x8d;
                        case 0x00e9:
                                return 0x8e;
                        case 0x00e8:
                                return 0x8f;
                        case 0x00ea:
                                return 0x90;
                        case 0x00eb:
                                return 0x91;
                        case 0x00ed:
                                return 0x92;
                        case 0x00ec:
                                return 0x93;
                        case 0x00ee:
                                return 0x94;
                        case 0x00ef:
                                return 0x95;
                        case 0x00f1:
                                return 0x96;
                        case 0x00f3:
                                return 0x97;
                        case 0x00f2:
                                return 0x98;
                        case 0x00f4:
                                return 0x99;
                        case 0x00f6:
                                return 0x9a;
                        case 0x00f5:
                                return 0x9b;
                        case 0x00fa:
                                return 0x9c;
                        case 0x00f9:
                                return 0x9d;
                        case 0x00fb:
                                return 0x9e;
                        case 0x00fc:
                                return 0x9f;
                        case 0x2020:
                                return 0xa0;
                        case 0x00b0:
                                return 0xa1;
                        case 0x00a2:
                                return 0xa2;
                        case 0x00a3:
                                return 0xa3;
                        case 0x00a7:
                                return 0xa4;
                        case 0x2022:
                                return 0xa5;
                        case 0x00b6:
                                return 0xa6;
                        case 0x00df:
                                return 0xa7;
                        case 0x00ae:
                                return 0xa8;
                        case 0x00a9:
                                return 0xa9;
                        case 0x2122:
                                return 0xaa;
                        case 0x00b4:
                                return 0xab;
                        case 0x00a8:
                                return 0xac;
                        case 0x2260:
                                return 0xad;
                        case 0x00c6:
                                return 0xae;
                        case 0x00d8:
                                return 0xaf;
                        case 0x221e:
                                return 0xb0;
                        case 0x00b1:
                                return 0xb1;
                        case 0x2264:
                                return 0xb2;
                        case 0x2265:
                                return 0xb3;
                        case 0x00a5:
                                return 0xb4;
                        case 0x00b5:
                                return 0xb5;
                        case 0x2202:
                                return 0xb6;
                        case 0x2211:
                                return 0xb7;
                        case 0x220f:
                                return 0xb8;
                        case 0x03c0:
                                return 0xb9;
                        case 0x222b:
                                return 0xba;
                        case 0x00aa:
                                return 0xbb;
                        case 0x00ba:
                                return 0xbc;
                        case 0x03a9:
                                return 0xbd;
                        case 0x00e6:
                                return 0xbe;
                        case 0x00f8:
                                return 0xbf;
                        case 0x00bf:
                                return 0xc0;
                        case 0x00a1:
                                return 0xc1;
                        case 0x00ac:
                                return 0xc2;
                        case 0x221a:
                                return 0xc3;
                        case 0x0192:
                                return 0xc4;
                        case 0x2248:
                                return 0xc5;
                        case 0x2206:
                                return 0xc6;
                        case 0x00AB:
                                return 0xc7;
                        case 0x00BB:
                                return 0xc8;
                        case 0x2026:
                                return 0xc9;
                        case 0x00A0:
                                return 0xca;
                        case 0x00C0:
                                return 0xcb;
                        case 0x00C3:
                                return 0xcc;
                        case 0x00D5:
                                return 0xcd;
                        case 0x0152:
                                return 0xce;
                        case 0x0153:
                                return 0xcf;
                        case 0x2013:
                                return 0xd0;
                        case 0x2014:
                                return 0xd1;
                        case 0x201c:
                                return 0xd2;
                        case 0x201d:
                                return 0xd3;
                        case 0x2018:
                                return 0xd4;
                        case 0x2019:
                                return 0xd5;
                        case 0x00f7:
                                return 0xd6;
                        case 0x25ca:
                                return 0xd7;
                        case 0x00ff:
                                return 0xd8;
                        case 0x0178:
                                return 0xd9;
                        case 0x2044:
                                return 0xda;
                        case 0x20ac:
                                return 0xdb;
                        case 0x2039:
                                return 0xdc;
                        case 0x203a:
                                return 0xdd;
                        case 0xfb01:
                                return 0xde;
                        case 0xfb02:
                                return 0xdf;
                        case 0x2021:
                                return 0xe0;
                        case 0x00b7:
                                return 0xe1;
                        case 0x201a:
                                return 0xe2;
                        case 0x201e:
                                return 0xe3;
                        case 0x2030:
                                return 0xe4;
                        case 0x00c2:
                                return 0xe5;
                        case 0x00ca:
                                return 0xe6;
                        case 0x00c1:
                                return 0xe7;
                        case 0x00cb:
                                return 0xe8;
                        case 0x00c8:
                                return 0xe9;
                        case 0x00cd:
                                return 0xea;
                        case 0x00ce:
                                return 0xeb;
                        case 0x00cf:
                                return 0xec;
                        case 0x00cc:
                                return 0xed;
                        case 0x00d3:
                                return 0xee;
                        case 0x00d4:
                                return 0xef;
                        case 0xf8ff:
                                return 0xf0;
                        case 0x00d2:
                                return 0xf1;
                        case 0x00da:
                                return 0xf2;
                        case 0x00db:
                                return 0xf3;
                        case 0x00d9:
                                return 0xf4;
                        case 0x0131:
                                return 0xf5;
                        case 0x02c6:
                                return 0xf6;
                        case 0x02dc:
                                return 0xf7;
                        case 0x00af:
                                return 0xf8;
                        case 0x02d8:
                                return 0xf9;
                        case 0x02d9:
                                return 0xfa;
                        case 0x02da:
                                return 0xfb;
                        case 0x00b8:
                                return 0xfc;
                        case 0x02dd:
                                return 0xfd;
                        case 0x02db:
                                return 0xfe;
                        case 0x02c7:
                                return 0xff;
                        default:
                                return 0xffffff3f;
                        }
                }
        };
        algos::apply_copy(codepoints_first, codepoints_last, destination,
                          [&encoding_was_total, encode](uint32_t x) {
                                  uint32_t result = encode(x);
                                  uint8_t macintosh_char = result & 0xff;
                                  if (result != macintosh_char) {
                                          encoding_was_total = false;
                                  }
                                  return macintosh_char;
                          });
        return encoding_was_total;
}
#endif