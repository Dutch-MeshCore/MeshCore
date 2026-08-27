#include <gtest/gtest.h>
#include "helpers/TxDutyWindow.h"

// Unit tests for TxDutyWindow: the rolling-window TX-airtime accumulator that
// yields the true TX duty cycle (airtime / wall-clock) as a 0..100 percent.
// The clock is injected (every call takes "now"), so these are pure and need
// no radio/mesh. Window is TxDutyWindow::WINDOW_MS (60 s of 10 s slots).

static const unsigned long W = TxDutyWindow::WINDOW_MS;   // 60000

// An untouched / idle window reads 0, and stays 0 as time passes with no TX.
TEST(TxDutyWindow, IdleReadsZero) {
  TxDutyWindow d;
  d.reset(1000);
  EXPECT_EQ(d.percent(1000), 0);
  EXPECT_EQ(d.percent(1000 + W), 0);
  EXPECT_EQ(d.percent(1000 + 10 * W), 0);
}

// Airtime equal to X% of the window reads back as X%.
TEST(TxDutyWindow, SteadyAirtimeGivesTruePercent) {
  TxDutyWindow d;
  d.reset(0);
  d.addAirtime(0, W / 10);          // 6000 ms of TX = 10% of the 60 s window
  EXPECT_EQ(d.percent(0), 10);
  TxDutyWindow d2;
  d2.reset(0);
  d2.addAirtime(0, W / 4);          // 25%
  EXPECT_EQ(d2.percent(0), 25);
}

// A duty cycle cannot exceed 100%, even if bogus/overlapping airtime is fed in.
TEST(TxDutyWindow, ClampsAt100) {
  TxDutyWindow d;
  d.reset(0);
  d.addAirtime(0, W);               // exactly the whole window
  EXPECT_EQ(d.percent(0), 100);
  d.addAirtime(0, W);               // more than the window
  EXPECT_EQ(d.percent(0), 100);
}

// Airtime credited in different slots accumulates within the window.
TEST(TxDutyWindow, MultipleSlotsAccumulate) {
  TxDutyWindow d;
  d.reset(0);
  d.addAirtime(0, 3000);                          // slot 0
  d.addAirtime(TxDutyWindow::SLOT_MS, 3000);      // slot 1, 10 s later
  EXPECT_EQ(d.percent(TxDutyWindow::SLOT_MS), 10);  // 6000 / 60000 = 10%
}

// Old airtime ages out: a burst is still counted just inside the window and
// gone once it is a full window old.
TEST(TxDutyWindow, SlidingExpiry) {
  TxDutyWindow d;
  d.reset(0);
  d.addAirtime(0, W / 10);          // 10% burst at t = 0
  EXPECT_EQ(d.percent(0), 10);
  EXPECT_EQ(d.percent(W - 5000), 10);   // still within the 60 s window
  EXPECT_EQ(d.percent(W), 0);           // now a full window old: aged out
}

// After a long idle gap the window is fully cleared (no stale airtime lingers).
TEST(TxDutyWindow, LongIdleClearsWindow) {
  TxDutyWindow d;
  d.reset(0);
  d.addAirtime(0, W / 2);           // 50% burst
  EXPECT_EQ(d.percent(0), 50);
  EXPECT_EQ(d.percent(100 * W), 0); // idle for 100 windows: back to 0
  d.addAirtime(100 * W, W / 5);     // and the window still works afterwards
  EXPECT_EQ(d.percent(100 * W), 20);
}
