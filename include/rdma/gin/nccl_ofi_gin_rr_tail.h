/*
 * Copyright (c) 2026      Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef NCCL_OFI_GIN_RR_TAIL_H_
#define NCCL_OFI_GIN_RR_TAIL_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "rdma/gin/nccl_ofi_gin_types.h" /* MAX_NUM_RAILS */

/**
 * @file nccl_ofi_gin_rr_tail.h
 *
 * Strict round-robin, one-unposted-tail-per-rail GIN doorbell policy
 * (OFI_NCCL_GIN_RR_TAIL_FLUSH). This header is a self-contained,
 * data-path-independent policy engine so the ownership and FI_MORE decisions
 * can be exercised directly by unit tests without any libfabric or CUDA
 * dependency.
 *
 * The engine never fabricates dummy WQEs. Every WQE that terminates a rail is
 * either the current real request or a previously-*retained* (never-posted)
 * real request. A posted WQE's FI_MORE flag is never mutated after the fact:
 * the retained tail is held UNPOSTED until either a newer real request proves
 * it is not last (post it WITH FI_MORE) or a boundary forces it to terminate
 * its rail (post it with no FI_MORE). Each request is therefore posted exactly
 * once.
 *
 * The engine is templated on a "token" type (the opaque per-rail retained
 * request handle) and a "sink" whose callbacks perform the concrete posting.
 * The real data path binds token = nccl_net_ofi_gin_write_req_t* and a sink
 * that calls post()/pending-queue transfer. Unit tests bind token = int and a
 * recording sink.
 *
 * All state transitions occur under the endpoint lock in the real path; the
 * engine adds no lock of its own.
 *
 * Policy summary (two-rail illustration, N = reqs_per_doorbell):
 *
 *   - Ordinary single-stripe put on rail r:
 *       * if rail r already holds a retained tail, that old tail is proven
 *         non-last, so post it WITH FI_MORE and free the slot;
 *       * if not at a group boundary, retain the current request UNPOSTED as
 *         rail r's new tail (its umbrella pending flag stays true);
 *       * at a group boundary (aggregate cleared, or the N-th op in the
 *         group), the current request terminates rail r with no FI_MORE and
 *         every other active rail's retained tail terminates its rail with no
 *         FI_MORE. No dummy WQEs are issued.
 *   - Signal-only op: the real metadata SEND terminates its RR-selected rail;
 *     a retained tail on that rail (if any) is proven non-last (post WITH
 *     FI_MORE) and all other rails' tails ring (no FI_MORE).
 *   - Put-with-signal / multi-stripe op: a touched rail's retained tail is
 *     proven non-last (post WITH FI_MORE); an untouched rail's retained tail
 *     rings (no FI_MORE). Stripes are never redirected off their scheduled
 *     rails.
 *   - Nonaggregate / final flush / close fail-safe: every remaining retained
 *     tail rings (no FI_MORE).
 */

/**
 * @brief Reasons a rail's retained tail (or the current request) is posted.
 */
enum nccl_ofi_gin_rr_post_kind_t {
	/* Retained tail posted WITH FI_MORE: a newer real request on the same
	   rail proves the old tail is not the last op on the rail. */
	RR_POST_TAIL_FI_MORE = 0,
	/* Retained tail posted WITHOUT FI_MORE: a boundary terminates the
	   rail and no newer request will land on it in this group. */
	RR_POST_TAIL_RING = 1,
	/* Current real request posted WITHOUT FI_MORE: it is the terminating
	   op for its rail at a boundary. */
	RR_POST_CURRENT_RING = 2,
};

/**
 * @brief Per-call outcome flags, mostly for assertions and observability.
 */
struct nccl_ofi_gin_rr_result_t {
	/* Number of retained tails posted with FI_MORE during this call. */
	uint16_t tails_posted_fi_more = 0;
	/* Number of retained tails posted with no FI_MORE (ring) this call. */
	uint16_t tails_posted_ring = 0;
	/* True if the current request terminated its rail (rang) this call. */
	bool current_rang = false;
	/* True if the current request was retained (held unposted) this call. */
	bool current_retained = false;
	/* True if a boundary group flush occurred this call. */
	bool group_flushed = false;
};

/**
 * @brief Strict RR one-tail-per-rail doorbell policy engine.
 *
 * Template parameters:
 *   Token   Opaque retained-request handle (pointer in the data path, int in
 *           tests). A default-constructed Token is the "empty" sentinel.
 *   Sink    Provides:
 *             void post_tail(Token tok, uint16_t rail, bool fi_more)
 *             void post_current(uint16_t rail, bool fi_more)
 *
 * The engine only *decides and orders* posts; the sink performs them. The
 * engine holds at most one retained Token per rail. A Token is passed to
 * post_tail() exactly once, after which its slot is cleared.
 */
template <typename Token>
class nccl_ofi_gin_rr_tail_engine_t {
public:
	explicit nccl_ofi_gin_rr_tail_engine_t(uint16_t num_rails_arg,
					       uint32_t reqs_per_doorbell_arg)
		: num_rails(num_rails_arg),
		  reqs_per_doorbell(reqs_per_doorbell_arg == 0 ? 1 : reqs_per_doorbell_arg)
	{
	}

	uint16_t get_num_rails() const { return num_rails; }
	uint32_t get_reqs_per_doorbell() const { return reqs_per_doorbell; }
	uint32_t get_group_count() const { return group_count; }
	uint16_t get_next_rail() const { return next_rail; }

	/**
	 * @brief Number of rails currently holding a retained tail.
	 */
	uint16_t open_tails() const
	{
		uint16_t n = 0;
		for (uint16_t r = 0; r < num_rails; r++) {
			if (has_tail[r]) {
				n++;
			}
		}
		return n;
	}

	bool rail_has_tail(uint16_t rail) const
	{
		return rail < num_rails && has_tail[rail];
	}

	Token peek_tail(uint16_t rail) const
	{
		return (rail < num_rails && has_tail[rail]) ? tail[rail] : Token {};
	}

	/**
	 * @brief Select and advance the strict round-robin rail.
	 *
	 * Returns the rail this op's stripe (and metadata, if any) rides. Each
	 * call advances the cursor by exactly one, so a sequence wraps across
	 * all rails in order.
	 */
	uint16_t select_rail()
	{
		uint16_t rail = next_rail;
		next_rail = static_cast<uint16_t>((next_rail + 1) % num_rails);
		return rail;
	}

	/**
	 * @brief Handle one ordinary single-stripe payload put on `rail`.
	 *
	 * `current` is the real request object for this op; `aggregate` is the
	 * caller's per-op batching hint. A boundary is reached when aggregate is
	 * cleared or the group has filled to reqs_per_doorbell. The sink posts
	 * as directed.
	 *
	 * Ownership contract: if the returned result.current_retained is true,
	 * the caller must NOT post `current`; the engine now owns it (held
	 * unposted). Otherwise `current` was posted via the sink.
	 */
	template <typename Sink>
	nccl_ofi_gin_rr_result_t on_single_stripe(Sink &sink, uint16_t rail,
						  Token current, bool aggregate)
	{
		nccl_ofi_gin_rr_result_t res {};

		const bool boundary =
			!aggregate || (group_count + 1 >= reqs_per_doorbell);

		/* A newer real request on this rail proves the old tail is not
		   last: post it WITH FI_MORE and free the slot. */
		if (has_tail[rail]) {
			sink.post_tail(tail[rail], rail, /*fi_more=*/true);
			clear_tail(rail);
			res.tails_posted_fi_more++;
		}

		if (!boundary) {
			/* Retain the current request unposted as this rail's new
			   tail. Its umbrella pending flag stays true. */
			set_tail(rail, current);
			group_count++;
			res.current_retained = true;
			return res;
		}

		/* Boundary: current terminates its rail; retained tails on all
		   other active rails terminate theirs. No dummy WQEs. */
		flush_other_tails(sink, rail, res);
		sink.post_current(rail, /*fi_more=*/false);
		res.current_rang = true;
		res.group_flushed = true;
		reset_group();
		return res;
	}

	/**
	 * @brief Handle a signal-only op (metadata SEND terminates its rail).
	 *
	 * If `rail` holds a retained payload tail, post it WITH FI_MORE (the
	 * metadata SEND that follows rings the rail); post retained tails on all
	 * other rails with no FI_MORE; then the caller posts the real metadata
	 * SEND on `rail` with no FI_MORE, terminating the rail. Resets the
	 * group.
	 */
	template <typename Sink>
	nccl_ofi_gin_rr_result_t on_signal_only(Sink &sink, uint16_t rail)
	{
		nccl_ofi_gin_rr_result_t res {};

		if (has_tail[rail]) {
			sink.post_tail(tail[rail], rail, /*fi_more=*/true);
			clear_tail(rail);
			res.tails_posted_fi_more++;
		}
		flush_other_tails(sink, rail, res);
		/* Caller posts the real metadata SEND on `rail` without FI_MORE;
		   it terminates the rail. */
		res.group_flushed = true;
		reset_group();
		return res;
	}

	/**
	 * @brief Handle a put-with-signal op.
	 *
	 * Flush retained tails on OTHER rails (no FI_MORE); the put's stripe
	 * posts WITH FI_MORE and its real metadata SEND terminates the stripe's
	 * rail (data-before-signal ordering). If `rail` already holds a retained
	 * tail, that old tail is proven non-last by the new stripe and is posted
	 * WITH FI_MORE first. Resets the group.
	 */
	template <typename Sink>
	nccl_ofi_gin_rr_result_t on_put_with_signal(Sink &sink, uint16_t rail)
	{
		nccl_ofi_gin_rr_result_t res {};

		if (has_tail[rail]) {
			sink.post_tail(tail[rail], rail, /*fi_more=*/true);
			clear_tail(rail);
			res.tails_posted_fi_more++;
		}
		flush_other_tails(sink, rail, res);
		res.group_flushed = true;
		reset_group();
		return res;
	}

	/**
	 * @brief Handle a multi-stripe op.
	 *
	 * Flush retained tails on rails NOT touched by this op (no FI_MORE); a
	 * touched rail's retained tail is proven non-last by the new stripe
	 * (post WITH FI_MORE). The stripes are NOT redirected. Reset the global
	 * group afterward.
	 *
	 * `touched_mask` bit r set means the op posts a real stripe on rail r.
	 */
	template <typename Sink>
	nccl_ofi_gin_rr_result_t on_multistripe(Sink &sink, uint64_t touched_mask)
	{
		nccl_ofi_gin_rr_result_t res {};

		for (uint16_t r = 0; r < num_rails; r++) {
			if (!has_tail[r]) {
				continue;
			}
			const bool touched = (touched_mask & (UINT64_C(1) << r)) != 0;
			if (touched) {
				/* Newer real stripe proves old tail non-last. */
				sink.post_tail(tail[r], r, /*fi_more=*/true);
				res.tails_posted_fi_more++;
			} else {
				sink.post_tail(tail[r], r, /*fi_more=*/false);
				res.tails_posted_ring++;
			}
			clear_tail(r);
		}
		res.group_flushed = true;
		reset_group();
		return res;
	}

	/**
	 * @brief Nonaggregate / final-flush drain of all retained tails.
	 *
	 * Post every remaining retained tail with no FI_MORE and reset. Returns
	 * the number of tails drained. Used both as a normal end-of-batch flush
	 * and as the close fail-safe.
	 */
	template <typename Sink>
	uint16_t drain_all(Sink &sink)
	{
		uint16_t n = 0;
		for (uint16_t r = 0; r < num_rails; r++) {
			if (has_tail[r]) {
				sink.post_tail(tail[r], r, /*fi_more=*/false);
				clear_tail(r);
				n++;
			}
		}
		reset_group();
		return n;
	}

	/**
	 * @brief Drop the retained tail on `rail` without posting.
	 *
	 * Used for EAGAIN ownership transfer: when a retained tail is posted
	 * and returns -FI_EAGAIN, the caller enqueues it on the existing
	 * pending queue and calls this to clear the engine's slot so ownership
	 * moves cleanly and the slot is not double-freed. Also used before the
	 * caller performs the post directly (so the engine no longer owns it).
	 */
	void abandon_tail(uint16_t rail)
	{
		if (rail < num_rails) {
			clear_tail(rail);
		}
	}

	/**
	 * @brief Reset all state (used on error teardown paths).
	 */
	void reset_all()
	{
		for (uint16_t r = 0; r < num_rails; r++) {
			clear_tail(r);
		}
		reset_group();
		next_rail = 0;
	}

	/**
	 * @brief Directly retain `tok` as `rail`'s tail and advance the group.
	 *
	 * Used by the data path when it has already posted any prior tail on
	 * the rail and decided (non-boundary) to hold the current request. The
	 * rail must not already hold a tail (at most one per rail).
	 */
	void retain_tail(uint16_t rail, Token tok)
	{
		set_tail(rail, tok);
		group_count++;
	}

	/**
	 * @brief Public reset of the group counter (data path boundary flush).
	 */
	void reset_group_public() { reset_group(); }

private:
	void set_tail(uint16_t rail, Token tok)
	{
		tail[rail] = tok;
		has_tail[rail] = true;
	}

	void clear_tail(uint16_t rail)
	{
		tail[rail] = Token {};
		has_tail[rail] = false;
	}

	void reset_group() { group_count = 0; }

	/* Post retained tails on every active rail EXCEPT `except_rail`, each
	   with no FI_MORE (they terminate their rails). */
	template <typename Sink>
	void flush_other_tails(Sink &sink, uint16_t except_rail,
			       nccl_ofi_gin_rr_result_t &res)
	{
		for (uint16_t r = 0; r < num_rails; r++) {
			if (r == except_rail || !has_tail[r]) {
				continue;
			}
			sink.post_tail(tail[r], r, /*fi_more=*/false);
			clear_tail(r);
			res.tails_posted_ring++;
		}
	}

	uint16_t num_rails;
	uint32_t reqs_per_doorbell;

	std::array<Token, MAX_NUM_RAILS> tail {};
	std::array<bool, MAX_NUM_RAILS> has_tail {};
	uint32_t group_count = 0;
	uint16_t next_rail = 0;
};

#endif /* NCCL_OFI_GIN_RR_TAIL_H_ */
