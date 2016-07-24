#pragma once
// Defines controlling output
#if !defined(ZWEI_SLOW)
#error ZWEI_SLOW={0,1} controls expensive assertions
#endif
#if !defined(ZWEI_INTERNAL)
#error ZWEI_INTERNAL={0,1} marks a build as internal or published
#endif
#define ZWEI_UNIT_TESTS (ZWEI_SLOW && ZWEI_INTERNAL)
