#pragma once

/**
   @file

   Dealing with dates and time is an important part of Zwei, in order
   to gather documents in a timeline.

*/

/**
   a CivilDateTime the human concept of a precise date on Earth.

   Default equality of two CivilDateTime is structural equality, not
   equivalence in the timeline. For equivalence and ordering you need
   to transform the values onto a timeline.
*/
struct CivilDateTime {
        using RegularMe = CivilDateTime;

        int16_t year;
        uint8_t month_count;        /// month in [1,12]
        uint8_t day_count;          /// day in [1,31]
        int8_t zone_hour_offset;    /// offset (-24,24)
        uint8_t zone_minute_offset; /// offset in [0,60)
        uint8_t hour;               /// hour in [0,24)
        uint8_t minute;             /// minute in [0,60)
        uint8_t seconds;            /// second in [0,60)

        CivilDateTime() = default;
        CivilDateTime(const RegularMe &x) = default;
        RegularMe &operator=(const RegularMe &x) = default;

        friend bool operator==(const RegularMe &a, const RegularMe &b)
        {
                return a.year == b.year && a.month_count == b.month_count &&
                       a.day_count == b.day_count &&
                       a.zone_hour_offset == b.zone_hour_offset &&
                       a.zone_minute_offset == b.zone_minute_offset &&
                       a.hour == b.hour && a.minute == b.minute &&
                       a.seconds == b.seconds;
        }

        friend bool operator!=(const RegularMe &a, const RegularMe &b)
        {
                return !(a == b);
        }
};
