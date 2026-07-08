#pragma once
//
// GPS epoch -> UTC conversion, for the DeviceTimeReq network-time reply.
//
// The RTC and LMIC glue need Arduino and stay in the .ino. This header holds the
// arithmetic, which is exactly the part that is easy to get subtly wrong and
// easy to test.
//
// ---------------------------------------------------------------------------
// Why this conversion is not a one-liner
// ---------------------------------------------------------------------------
//
// LMIC's DeviceTimeReq returns GPS time, not UTC. Two corrections:
//
//   1. GPS epoch started 1980-01-06, Unix epoch 1970-01-01. The offset is
//      315964800 seconds.
//   2. GPS time does NOT count leap seconds; UTC does. So UTC = GPS - leap
//      seconds. As of 2026 the count is 18. This changes, rarely, by IERS
//      bulletin -- keep it here, dated, where it can be found.
//
// There is no LMIC helper for this. The constants are ours to own.
//
#include <stdint.h>

// Seconds between the Unix epoch (1970-01-01) and the GPS epoch (1980-01-06).
#define GPS_UNIX_OFFSET 315964800u

// GPS-UTC leap-second count. 18 since 2017-01-01. Verify against the IERS
// bulletin if a leap second is ever announced (they are announced ~6 months
// ahead). Last checked: 2026-07.
#define GPS_UTC_LEAP_SECONDS 18u

// Convert a GPS second count to a Unix UTC timestamp.
//
// elapsedMsSinceSample compensates for the delay between when LMIC sampled the
// network time (end of the uplink) and when we act on it (after the RX window).
// The callback runs seconds later, so without this the clock lands seconds slow.
inline uint32_t gpsToUnixUtc(uint32_t gpsSeconds, uint32_t elapsedMsSinceSample) {
  return gpsSeconds + GPS_UNIX_OFFSET - GPS_UTC_LEAP_SECONDS
       + (elapsedMsSinceSample / 1000u);
}

// A network-time reply is only believable within a sane window. A zero, or a
// time in the distant past, means the reply did not really land -- reject it
// rather than setting the RTC to nonsense.
//
// Lower bound: 2020-01-01 (1577836800). Upper bound: 2050-01-01 (2524608000).
// Anything outside is not a real "now" for this device.
#define UTC_PLAUSIBLE_MIN 1577836800u
#define UTC_PLAUSIBLE_MAX 2524608000u

inline bool utcPlausible(uint32_t unixUtc) {
  return unixUtc >= UTC_PLAUSIBLE_MIN && unixUtc <= UTC_PLAUSIBLE_MAX;
}
