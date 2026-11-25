// ===========================================================================
//  calibration.h - the taught site tables for the bench experiments
//
//  WHAT IT IS
//  ----------
//  The numbers found with the site-calibration procedure (docs/site-calibration.md):
//  for each hand, for each of the five sites,
//    AIM     = the distal (EMAX) pulse that puts the tube above the site's pen dot
//    CONTACT = the proximal (PDI) pulse at which the tube FIRST touches the skin
//  and the two constants that are the same for every participant:
//    PRESS_US (P) = how far PAST the skin the tap is commanded
//    START_US (S) = how far ABOVE the skin the tap starts
//
//  From them a program computes, per site (fewer us = lower on the proximal):
//    RAISED = CONTACT + S      where the tap starts
//    TOUCH  = CONTACT - P      where the tap ends
//  Worked example, CONTACT 1610, S 100, P 10:
//    RAISED = 1610 + 100 = 1710 us,  TOUCH = 1610 - 10 = 1600 us,
//    so the stroke is 110 us (about 10 degrees) at EVERY site: same movement,
//    same delay from command to contact (PROJECT.md 2.2, design D4).
//
//  HOW TO FILL IT
//  --------------
//  1. Teach the hand with arm_jog (key mode: c + site number records a site,
//     p prints a block ready to paste).
//  2. Paste the block over RUBBER (top arm) or PARTICIPANT (down arm) below,
//     and fill in the who / date text.
//  3. Rebuild and upload arm_taps.
//  A 0 means "not taught": arm_taps refuses to tap a hand with a 0 in its table.
//
//  Used by firmware/experiments/arm_taps (through -I../common in its
//  platformio.ini). The real firmware will receive the same numbers from the
//  host with CAL SET instead (PROJECT.md Step 4.1).
//
//  Plain data only: no code, no heap, nothing that runs.
// ===========================================================================
#pragma once      // include this file at most once per build

#include <cstdint>  // uint8_t, uint16_t: integers of a known width

namespace calib {

// ---------------------------------------------------------------------------
// The constants shared by every participant. P and S are open parameters
// (PROJECT.md 1.12, U1 and U4): these are the bench starting values.
// At about 200 mm of reach, 10 us on the PDI is about 0.93 degrees, which moves
// the tube about 3 mm.
// ---------------------------------------------------------------------------
constexpr uint8_t  N_SITES  = 5;    // five sites on the index finger, as in Chancel et al. (PROJECT.md 1.4)
constexpr uint16_t PRESS_US = 10;   // P: commanded press past contact, 10 us = about 3 mm
constexpr uint16_t START_US = 100;  // S: start height above contact, 100 us = about 3 cm

// ---------------------------------------------------------------------------
// HandSites: the taught table of one hand. Site 1 is the proximal phalanx
// (nearest the knuckle), site 5 the distal phalanx just behind the nail.
// Index 0 of each array is site 1.
// ---------------------------------------------------------------------------
struct HandSites {
  const char* who;                // which hand: "rubber hand" or a participant code
  const char* date;               // when it was taught, YYYY-MM-DD
  uint16_t    aim[N_SITES];       // AIM: distal pulse over each site, us
  uint16_t    contact[N_SITES];   // CONTACT: proximal pulse at first skin contact, us
};

// ---------------------------------------------------------------------------
// The rubber hand, tapped by the TOP arm (PDI on pin 3, EMAX on pin 6).
// Taught once; teach again only if the rubber hand or its mount is moved.
// ---------------------------------------------------------------------------
inline constexpr HandSites RUBBER = {
    "rubber hand",                   // who
    "2026-11-25",                    // date
    {1265, 1185, 1140, 1130, 1110},  // AIM: distal pulse per site 1-5, us
    {1600, 1590, 1550, 1530, 1505}}; // CONTACT: proximal pulse per site 1-5, us
// Worked example, site 3: RAISED = 1550 + S (100) = 1650 us, TOUCH = 1550 - P (10)
// = 1540 us. Highest RAISED is site 1 (1700), so TRAVEL 100 gives 1800 us.

// ---------------------------------------------------------------------------
// The current participant's real hand, tapped by the DOWN arm (PDI on pin 2,
// EMAX on pin 9). Replaced at the start of every session.
// ---------------------------------------------------------------------------
inline constexpr HandSites PARTICIPANT = {
    "test participant (author)",     // who
    "2026-11-26",                    // date
    {1170, 1050, 990, 980, 1000},    // AIM: distal pulse per site 1-5, us
    {1620, 1610, 1575, 1540, 1515}}; // CONTACT: proximal pulse per site 1-5, us
// Worked example, site 3: RAISED = 1575 + 100 = 1675 us, TOUCH = 1575 - 10 =
// 1565 us. Highest RAISED is site 1 (1720), so TRAVEL 100 gives 1820 us.
// Site 5's AIM (1000) is above site 4's (980), the reverse of the trend toward
// the tip: check in the ink test that the site 5 mark lands beyond site 4's.

}  // namespace calib
