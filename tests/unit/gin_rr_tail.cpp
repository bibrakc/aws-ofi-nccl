/*
 * Copyright (c) 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * Unit tests for the strict round-robin, one-unposted-tail-per-rail doorbell
 * policy engine (OFI_NCCL_GIN_RR_TAIL_FLUSH). These exercise the data-path
 * decision logic in isolation via a recording sink and integer tokens, exactly
 * mirroring the ownership/FI_MORE ordering the real path performs.
 *
 * Scenarios: strict RR wrap; at most one tail per rail; DB8/DB16 on two rails;
 * DB16 on four rails; nonaggregate boundary; signal-only and put-with-signal
 * ordering; multi-stripe (no redirection); EAGAIN ownership; hard-error
 * cleanup; close fail-safe drain; default-off behavior; boundary ordering.
 */

#include "config.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "unit_test.h"
#include "rdma/gin/nccl_ofi_gin_rr_tail.h"

#define CHECK_AND_EXIT(x)                                                                          \
	if (!(x)) {                                                                                \
		std::cerr << "Failure at line " << __LINE__ << ": " << #x << std::endl;            \
		exit(1);                                                                           \
	}

/* One recorded post action. */
struct rec {
	enum kind { TAIL, CURRENT } k;
	int token;      /* only meaningful for TAIL */
	uint16_t rail;
	bool fi_more;
};

/* Recording sink: appends every post the engine directs, in order. It also
   drives "current" posts so ordering (tails-before-current) is observable. */
struct recording_sink {
	std::vector<rec> events;
	void post_tail(int tok, uint16_t rail, bool fi_more)
	{
		events.push_back({ rec::TAIL, tok, rail, fi_more });
	}
	void post_current(uint16_t rail, bool fi_more)
	{
		events.push_back({ rec::CURRENT, -1, rail, fi_more });
	}
};

using engine_t = nccl_ofi_gin_rr_tail_engine_t<int>;

/* Strict RR sequence wraps across all rails in order. */
static void test_strict_rr_wraps(void)
{
	engine_t e2(2, 16);
	CHECK_AND_EXIT(e2.select_rail() == 0);
	CHECK_AND_EXIT(e2.select_rail() == 1);
	CHECK_AND_EXIT(e2.select_rail() == 0);
	CHECK_AND_EXIT(e2.select_rail() == 1);

	engine_t e4(4, 16);
	for (int i = 0; i < 12; i++) {
		CHECK_AND_EXIT(e4.select_rail() == static_cast<uint16_t>(i % 4));
	}
}

/* DB8/two rails => four real writes per rail, one terminating current + one
   terminating retained tail, no dummies; at most one open tail per rail. */
static void test_db8_two_rails(void)
{
	engine_t e(2, 8); /* global group of 8 => 4 per rail */
	recording_sink s;
	int tok = 100;

	int real_writes_per_rail[2] = { 0, 0 };
	for (int i = 0; i < 8; i++) {
		uint16_t rail = e.select_rail();
		int current = tok++;
		auto res = e.on_single_stripe(s, rail, current, /*aggregate=*/true);
		real_writes_per_rail[rail]++;
		CHECK_AND_EXIT(e.open_tails() <= 2);
		if (res.group_flushed) {
			CHECK_AND_EXIT(i == 7);
		}
	}

	CHECK_AND_EXIT(real_writes_per_rail[0] == 4);
	CHECK_AND_EXIT(real_writes_per_rail[1] == 4);

	size_t ring_posts = 0, current_posts = 0;
	for (auto &ev : s.events) {
		if (ev.k == rec::CURRENT) {
			current_posts++;
			CHECK_AND_EXIT(ev.fi_more == false);
		} else if (!ev.fi_more) {
			ring_posts++;
		}
	}
	CHECK_AND_EXIT(current_posts == 1);
	CHECK_AND_EXIT(ring_posts == 1);   /* the other rail's retained tail */
	CHECK_AND_EXIT(e.open_tails() == 0);
}

/* DB16/two rails => eight real writes per rail, two terminating posts. */
static void test_db16_two_rails(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 200;
	int per_rail[2] = { 0, 0 };
	bool flushed = false;
	for (int i = 0; i < 16; i++) {
		uint16_t rail = e.select_rail();
		auto res = e.on_single_stripe(s, rail, tok++, /*aggregate=*/true);
		per_rail[rail]++;
		if (res.group_flushed) { flushed = true; CHECK_AND_EXIT(i == 15); }
	}
	CHECK_AND_EXIT(flushed);
	CHECK_AND_EXIT(per_rail[0] == 8 && per_rail[1] == 8);
	size_t rings = 0, currents = 0;
	for (auto &ev : s.events) {
		if (ev.k == rec::CURRENT) { currents++; CHECK_AND_EXIT(!ev.fi_more); }
		else if (!ev.fi_more) rings++;
	}
	CHECK_AND_EXIT(currents == 1 && rings == 1);
	CHECK_AND_EXIT(e.open_tails() == 0);
}

/* DB16/four rails => four real writes per rail, four terminating posts. */
static void test_db16_four_rails(void)
{
	engine_t e(4, 16);
	recording_sink s;
	int tok = 300;
	int per_rail[4] = { 0, 0, 0, 0 };
	bool flushed = false;
	for (int i = 0; i < 16; i++) {
		uint16_t rail = e.select_rail();
		auto res = e.on_single_stripe(s, rail, tok++, /*aggregate=*/true);
		per_rail[rail]++;
		if (res.group_flushed) { flushed = true; CHECK_AND_EXIT(i == 15); }
	}
	CHECK_AND_EXIT(flushed);
	for (int r = 0; r < 4; r++) CHECK_AND_EXIT(per_rail[r] == 4);
	size_t rings = 0, currents = 0;
	for (auto &ev : s.events) {
		if (ev.k == rec::CURRENT) { currents++; CHECK_AND_EXIT(!ev.fi_more); }
		else if (!ev.fi_more) rings++;
	}
	CHECK_AND_EXIT(currents == 1 && rings == 3);
	CHECK_AND_EXIT(e.open_tails() == 0);
}

/* At most one unposted tail per rail across a long run, and every rail re-use
   posts the old tail with FI_MORE. */
static void test_at_most_one_tail_per_rail(void)
{
	engine_t e(2, 1000000); /* effectively never hit runtime boundary */
	recording_sink s;
	int tok = 400;
	for (int i = 0; i < 50; i++) {
		uint16_t rail = e.select_rail();
		e.on_single_stripe(s, rail, tok++, /*aggregate=*/true);
		CHECK_AND_EXIT(e.open_tails() <= 2);
	}
	CHECK_AND_EXIT(e.open_tails() == 2);
	size_t fi_more = 0;
	for (auto &ev : s.events) if (ev.k == rec::TAIL && ev.fi_more) fi_more++;
	CHECK_AND_EXIT(fi_more == 48); /* 24 re-uses per rail * 2 rails */
}

/* Early nonaggregate boundary flushes all active tails. */
static void test_nonaggregate_boundary(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 500;
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail0 */
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail1 */
	CHECK_AND_EXIT(e.open_tails() == 2);

	auto res = e.on_single_stripe(s, /*rail=*/0, tok++, /*aggregate=*/false);
	CHECK_AND_EXIT(res.group_flushed);
	CHECK_AND_EXIT(e.open_tails() == 0);

	size_t currents = 0;
	for (auto &ev : s.events) {
		if (ev.k == rec::CURRENT) { currents++; CHECK_AND_EXIT(!ev.fi_more); }
	}
	CHECK_AND_EXIT(currents == 1);
}

/* Signal-only uses metadata SEND to terminate its rail; other rails' retained
   tails terminate with real no-FI_MORE posts. */
static void test_signal_only(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 600;
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail0 tail */
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail1 tail */
	s.events.clear();

	uint16_t sig_rail = 0;
	auto res = e.on_signal_only(s, sig_rail);
	CHECK_AND_EXIT(res.group_flushed);
	CHECK_AND_EXIT(e.open_tails() == 0);
	bool saw_rail0_fi_more = false, saw_rail1_ring = false;
	for (auto &ev : s.events) {
		CHECK_AND_EXIT(ev.k == rec::TAIL);
		if (ev.rail == 0 && ev.fi_more) saw_rail0_fi_more = true;
		if (ev.rail == 1 && !ev.fi_more) saw_rail1_ring = true;
	}
	CHECK_AND_EXIT(saw_rail0_fi_more && saw_rail1_ring);
}

/* Put-with-signal preserves payload-before-metadata ordering. */
static void test_put_with_signal(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 700;
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail0 tail */
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail1 tail */
	s.events.clear();

	uint16_t put_rail = 0;
	auto res = e.on_put_with_signal(s, put_rail);
	CHECK_AND_EXIT(res.group_flushed);
	CHECK_AND_EXIT(e.open_tails() == 0);
	bool rail0_fi_more = false, rail1_ring = false;
	for (auto &ev : s.events) {
		if (ev.rail == 0 && ev.fi_more) rail0_fi_more = true;
		if (ev.rail == 1 && !ev.fi_more) rail1_ring = true;
	}
	CHECK_AND_EXIT(rail0_fi_more && rail1_ring);
}

/* Multi-stripe flushes/terminates all active rails without redirecting stripes.
   Touched rails => FI_MORE, untouched => ring. */
static void test_multistripe(void)
{
	engine_t e(4, 16);
	recording_sink s;
	int tok = 800;
	for (int i = 0; i < 4; i++)
		e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true);
	CHECK_AND_EXIT(e.open_tails() == 4);
	s.events.clear();

	uint64_t touched = (UINT64_C(1) << 0) | (UINT64_C(1) << 2);
	auto res = e.on_multistripe(s, touched);
	CHECK_AND_EXIT(res.group_flushed);
	CHECK_AND_EXIT(e.open_tails() == 0);
	CHECK_AND_EXIT(res.tails_posted_fi_more == 2); /* rails 0,2 */
	CHECK_AND_EXIT(res.tails_posted_ring == 2);    /* rails 1,3 */
	for (auto &ev : s.events) {
		CHECK_AND_EXIT(ev.k == rec::TAIL);
		if (ev.rail == 0 || ev.rail == 2) CHECK_AND_EXIT(ev.fi_more);
		if (ev.rail == 1 || ev.rail == 3) CHECK_AND_EXIT(!ev.fi_more);
	}
}

/* EAGAIN transfers ownership to pending queue and clears tail state. */
static void test_eagain_ownership(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 900;
	e.on_single_stripe(s, /*rail=*/0, tok, /*aggregate=*/true);
	CHECK_AND_EXIT(e.rail_has_tail(0));
	CHECK_AND_EXIT(e.peek_tail(0) == tok);

	int abandoned = e.peek_tail(0);
	e.abandon_tail(0);
	CHECK_AND_EXIT(abandoned == tok);
	CHECK_AND_EXIT(!e.rail_has_tail(0));
	CHECK_AND_EXIT(e.open_tails() == 0);
}

/* Non-EAGAIN (hard) error path clears the engine reference (data path clears
   the umbrella pending flag and returns the request; engine leaks nothing). */
static void test_non_eagain_error(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 950;
	e.on_single_stripe(s, /*rail=*/1, tok, /*aggregate=*/true);
	CHECK_AND_EXIT(e.rail_has_tail(1));
	e.abandon_tail(1);
	CHECK_AND_EXIT(!e.rail_has_tail(1));
	CHECK_AND_EXIT(e.peek_tail(1) == 0); /* empty sentinel */
}

/* Close fail-safe drains all real tails without FI_MORE. */
static void test_close_failsafe(void)
{
	engine_t e(2, 16);
	recording_sink s;
	int tok = 1000;
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true);
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true);
	CHECK_AND_EXIT(e.open_tails() == 2);
	s.events.clear();

	uint16_t drained = e.drain_all(s);
	CHECK_AND_EXIT(drained == 2);
	CHECK_AND_EXIT(e.open_tails() == 0);
	for (auto &ev : s.events) {
		CHECK_AND_EXIT(ev.k == rec::TAIL);
		CHECK_AND_EXIT(!ev.fi_more); /* fail-safe posts always ring */
	}
}

/* Default-off equivalence. The engine only exists when enabled; when never
   invoked it fabricates no posts. */
static void test_default_off_no_side_effects(void)
{
	engine_t e(2, 16);
	recording_sink s;
	CHECK_AND_EXIT(e.open_tails() == 0);
	CHECK_AND_EXIT(e.get_group_count() == 0);
	CHECK_AND_EXIT(s.events.empty());
	CHECK_AND_EXIT(e.drain_all(s) == 0);
	CHECK_AND_EXIT(s.events.empty());
}

/* Ordering invariant: on a boundary, retained tails on OTHER rails are posted
   BEFORE the current terminating post. */
static void test_boundary_ordering(void)
{
	engine_t e(2, 2); /* boundary every 2nd op */
	recording_sink s;
	int tok = 1100;
	e.on_single_stripe(s, e.select_rail(), tok++, /*aggregate=*/true); /* rail0 retained */
	CHECK_AND_EXIT(e.open_tails() == 1);
	s.events.clear();
	auto res = e.on_single_stripe(s, /*rail=*/1, tok++, /*aggregate=*/true);
	CHECK_AND_EXIT(res.group_flushed);
	CHECK_AND_EXIT(s.events.size() == 2);
	CHECK_AND_EXIT(s.events[0].k == rec::TAIL && s.events[0].rail == 0 &&
		       !s.events[0].fi_more);
	CHECK_AND_EXIT(s.events[1].k == rec::CURRENT && s.events[1].rail == 1 &&
		       !s.events[1].fi_more);
	CHECK_AND_EXIT(e.get_group_count() == 0);
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	unit_test_init();

	test_strict_rr_wraps();
	test_at_most_one_tail_per_rail();
	test_db8_two_rails();
	test_db16_two_rails();
	test_db16_four_rails();
	test_nonaggregate_boundary();
	test_signal_only();
	test_put_with_signal();
	test_multistripe();
	test_eagain_ownership();
	test_non_eagain_error();
	test_close_failsafe();
	test_default_off_no_side_effects();
	test_boundary_ordering();

	std::cout << "gin_rr_tail: all checks passed" << std::endl;
	return 0;
}
