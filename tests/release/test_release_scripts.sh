#!/bin/bash
#
# Copyright (c) 2026      Amazon.com, Inc. or its affiliates. All rights reserved.
#
# See LICENSE.txt for license information
#
# Focused portable tests for the release version parser, changelog generation,
# and tag/version behavior. Covers:
#   - Accepted/rejected tags
#   - NCCLOFI-1614: alpha/final and later-created old patch
#   - Exact tagger metadata
#   - Same-commit tag ambiguity / PLUGIN_TAG
#   - Relevant failure paths
#

set -euo pipefail

# Locate scripts relative to this test file
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${TEST_DIR}/../.." && pwd)"
RELEASE_VERSION="${REPO_ROOT}/contrib/scripts/release_version"
GENERATE_CHANGELOG="${REPO_ROOT}/contrib/scripts/generate_debian_changelog.sh"
GET_VERSION="${REPO_ROOT}/m4/get_version.sh"
GENERATE_TAG="${REPO_ROOT}/contrib/scripts/generate_release_tag"

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() {
	TESTS_PASSED=$((TESTS_PASSED + 1))
	TESTS_RUN=$((TESTS_RUN + 1))
	printf '  PASS: %s\n' "$1"
}

fail() {
	TESTS_FAILED=$((TESTS_FAILED + 1))
	TESTS_RUN=$((TESTS_RUN + 1))
	printf '  FAIL: %s\n' "$1" >&2
}

assert_eq() {
	local desc="$1" expected="$2" actual="$3"
	if [[ "${expected}" == "${actual}" ]]; then
		pass "${desc}"
	else
		fail "${desc}: expected='${expected}', actual='${actual}'"
	fi
}

assert_success() {
	local desc="$1"
	shift
	if "$@" > /dev/null 2>&1; then
		pass "${desc}"
	else
		fail "${desc}: command failed: $*"
	fi
}

assert_failure() {
	local desc="$1"
	shift
	if "$@" > /dev/null 2>&1; then
		fail "${desc}: command succeeded but should have failed: $*"
	else
		pass "${desc}"
	fi
}

# ===========================================================================
echo "=== Test Suite: release_version parser ==="
# ===========================================================================

echo ""
echo "--- Accepted tags ---"

assert_success "accept v1.22.0" "${RELEASE_VERSION}" --validate v1.22.0
assert_success "accept v1.22.0a1" "${RELEASE_VERSION}" --validate v1.22.0a1
assert_success "accept v1.22.0a2" "${RELEASE_VERSION}" --validate v1.22.0a2
assert_success "accept v10.20.30a999" "${RELEASE_VERSION}" --validate v10.20.30a999
assert_success "accept v0.0.0" "${RELEASE_VERSION}" --validate v0.0.0
assert_success "accept v0.0.1a1" "${RELEASE_VERSION}" --validate v0.0.1a1

echo ""
echo "--- Rejected tags ---"

assert_failure "reject 1.22.0 (no v prefix)" "${RELEASE_VERSION}" --validate 1.22.0
assert_failure "reject v1.22 (missing patch)" "${RELEASE_VERSION}" --validate v1.22
assert_failure "reject v1.22.0a (missing alpha number)" "${RELEASE_VERSION}" --validate v1.22.0a
assert_failure "reject v1.22.0a0 (zero alpha)" "${RELEASE_VERSION}" --validate v1.22.0a0
assert_failure "reject v1.22.0-alpha.1 (wrong alpha format)" "${RELEASE_VERSION}" --validate v1.22.0-alpha.1
assert_failure "reject v1.22.0rc1 (rc not supported)" "${RELEASE_VERSION}" --validate v1.22.0rc1
assert_failure "reject v1.22.0garbage (trailing text)" "${RELEASE_VERSION}" --validate v1.22.0garbage
assert_failure "reject prefix-v1.22.0 (leading text)" "${RELEASE_VERSION}" --validate prefix-v1.22.0
assert_failure "reject v1.22.0a1-extra (trailing after alpha)" "${RELEASE_VERSION}" --validate v1.22.0a1-extra
assert_failure "reject empty string" "${RELEASE_VERSION}" --validate ""
assert_failure "reject v1.22.0a01 (leading zero in alpha)" "${RELEASE_VERSION}" --validate v1.22.0a01

echo ""
echo "--- Field extraction (final) ---"

eval "$("${RELEASE_VERSION}" v1.22.0)"
assert_eq "TAG" "v1.22.0" "${TAG}"
assert_eq "VERSION" "1.22.0" "${VERSION}"
assert_eq "BASE_VERSION" "1.22.0" "${BASE_VERSION}"
assert_eq "MAJOR" "1" "${MAJOR}"
assert_eq "MINOR" "22" "${MINOR}"
assert_eq "PATCH" "0" "${PATCH}"
assert_eq "IS_ALPHA" "false" "${IS_ALPHA}"
assert_eq "ALPHA_SEQUENCE (empty)" "" "${ALPHA_SEQUENCE}"
assert_eq "TARBALL" "aws-ofi-nccl-1.22.0.tar.gz" "${TARBALL}"
assert_eq "DSC" "aws-ofi-nccl_1.22.0-1.dsc" "${DSC}"
assert_eq "DEBIAN_TAR" "aws-ofi-nccl_1.22.0-1.tar.xz" "${DEBIAN_TAR}"
assert_eq "SRPM" "libnccl-ofi-1.22.0-1.src.rpm" "${SRPM}"
assert_eq "DEBIAN_VERSION" "1.22.0-1" "${DEBIAN_VERSION}"
assert_eq "RELEASE_BRANCH" "v1.22.x" "${RELEASE_BRANCH}"

echo ""
echo "--- Field extraction (alpha) ---"

eval "$("${RELEASE_VERSION}" v1.22.0a1)"
assert_eq "TAG alpha" "v1.22.0a1" "${TAG}"
assert_eq "VERSION alpha" "1.22.0a1" "${VERSION}"
assert_eq "BASE_VERSION alpha" "1.22.0" "${BASE_VERSION}"
assert_eq "IS_ALPHA alpha" "true" "${IS_ALPHA}"
assert_eq "ALPHA_SEQUENCE alpha" "1" "${ALPHA_SEQUENCE}"
assert_eq "TARBALL alpha" "aws-ofi-nccl-1.22.0a1.tar.gz" "${TARBALL}"
assert_eq "DSC alpha" "aws-ofi-nccl_1.22.0a1-1.dsc" "${DSC}"
assert_eq "DEBIAN_TAR alpha" "aws-ofi-nccl_1.22.0a1-1.tar.xz" "${DEBIAN_TAR}"
assert_eq "SRPM alpha" "libnccl-ofi-1.22.0a1-1.src.rpm" "${SRPM}"
assert_eq "DEBIAN_VERSION alpha" "1.22.0a1-1" "${DEBIAN_VERSION}"
assert_eq "RELEASE_BRANCH alpha" "v1.22.x" "${RELEASE_BRANCH}"

echo ""
echo "--- --field mode ---"

field_ver="$("${RELEASE_VERSION}" --field VERSION v10.20.30a999)"
assert_eq "--field VERSION" "10.20.30a999" "${field_ver}"

field_branch="$("${RELEASE_VERSION}" --field RELEASE_BRANCH v3.4.5)"
assert_eq "--field RELEASE_BRANCH" "v3.4.x" "${field_branch}"

# ===========================================================================
echo ""
echo "=== Test Suite: NCCLOFI-1614 regression ==="
# ===========================================================================

# Create a temporary git repo to simulate the NCCLOFI-1614 scenario
TMPDIR="$(mktemp -d)"
trap 'rm -rf "${TMPDIR}"' EXIT

(
	cd "${TMPDIR}"
	git init -q repo
	cd repo
	git config user.email "test@example.com"
	git config user.name "Test User"

	# Initial commit on master
	echo "init" > file.txt
	git add file.txt
	git commit -q -m "Initial commit"
	INITIAL_SHA="$(git rev-parse HEAD)"

	# Create release branch v1.22.x
	git checkout -q -b v1.22.x

	# Create alpha 1 tag
	echo "alpha1" >> file.txt
	git add file.txt
	git commit -q -m "Alpha 1 changes"
	git tag -a -m "Create v1.22.0a1 release tag" v1.22.0a1

	# Another commit for alpha 2
	echo "alpha2" >> file.txt
	git add file.txt
	git commit -q -m "Alpha 2 changes"
	git tag -a -m "Create v1.22.0a2 release tag" v1.22.0a2

	# Final release commit
	echo "final" >> file.txt
	git add file.txt
	git commit -q -m "Final release"
	git tag -a -m "Create v1.22.0 release tag" v1.22.0

	# Simulate later-created old patch: create v1.21.1 AFTER v1.22.0
	# Branch from the initial commit
	git checkout -q -b v1.21.x "${INITIAL_SHA}"
	echo "backport" >> file.txt
	git add file.txt
	git commit -q -m "Backport fix"
	sleep 1  # Ensure different tagger date
	git tag -a -m "Create v1.21.1 release tag" v1.21.1
)

echo ""
echo "--- Exact tag determines version (not sort order) ---"

# Test that requesting v1.22.0 produces 1.22.0 (not 1.22.0a2 from sorting)
(
	cd "${TMPDIR}/repo"
	git checkout -q v1.22.x

	# The changelog script must produce the correct version for v1.22.0
	output="$("${GENERATE_CHANGELOG}" v1.22.0)"
	if echo "${output}" | head -1 | grep -q "aws-ofi-nccl (1.22.0-1)"; then
		pass "NCCLOFI-1614: v1.22.0 produces 1.22.0-1 changelog"
	else
		fail "NCCLOFI-1614: v1.22.0 changelog first line: $(echo "${output}" | head -1)"
	fi
)

# Test that requesting v1.22.0a2 produces 1.22.0a2
(
	cd "${TMPDIR}/repo"
	output="$("${GENERATE_CHANGELOG}" v1.22.0a2)"
	if echo "${output}" | head -1 | grep -q "aws-ofi-nccl (1.22.0a2-1)"; then
		pass "NCCLOFI-1614: v1.22.0a2 produces 1.22.0a2-1 changelog"
	else
		fail "NCCLOFI-1614: v1.22.0a2 changelog first line: $(echo "${output}" | head -1)"
	fi
)

# Test that later-created v1.21.1 does not affect v1.22.0 changelog
(
	cd "${TMPDIR}/repo"
	output="$("${GENERATE_CHANGELOG}" v1.22.0)"
	if echo "${output}" | grep -q "1.21.1"; then
		fail "NCCLOFI-1614: v1.21.1 leaked into v1.22.0 changelog"
	else
		pass "NCCLOFI-1614: later-created v1.21.1 does not affect v1.22.0"
	fi
)

# ===========================================================================
echo ""
echo "=== Test Suite: Exact tagger metadata ==="
# ===========================================================================

(
	cd "${TMPDIR}/repo"
	# v1.22.0 was tagged by "Test User <test@example.com>"
	output="$("${GENERATE_CHANGELOG}" v1.22.0)"
	if echo "${output}" | grep -q "Test User"; then
		pass "tagger name from exact tag"
	else
		fail "tagger name not found in changelog: ${output}"
	fi
	if echo "${output}" | grep -q "<test@example.com>"; then
		pass "tagger email from exact tag"
	else
		fail "tagger email not found in changelog: ${output}"
	fi
	if echo "${output}" | grep -Eq '^ -- Test User <test@example.com>  [A-Z][a-z]{2}, [ 0-9][0-9] [A-Z][a-z]{2} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2} [+-][0-9]{4}$'; then
		pass "tagger date uses Debian-compatible RFC 2822 format"
	else
		fail "tagger date is not RFC 2822 formatted: ${output}"
	fi
)

# ===========================================================================
echo ""
echo "=== Test Suite: Alpha/final release description ==="
# ===========================================================================

(
	cd "${TMPDIR}/repo"
	output="$("${GENERATE_CHANGELOG}" v1.22.0a2)"
	if echo "${output}" | grep -q "New upstream prerelease 1.22.0a2"; then
		pass "alpha changelog uses 'prerelease' description"
	else
		fail "alpha changelog missing prerelease description: ${output}"
	fi

	output="$("${GENERATE_CHANGELOG}" v1.22.0)"
	if echo "${output}" | grep -q "New upstream release 1.22.0"; then
		pass "final changelog uses 'release' description"
	else
		fail "final changelog missing release description: ${output}"
	fi
)

# ===========================================================================
echo ""
echo "=== Test Suite: Same-commit tag ambiguity / PLUGIN_TAG ==="
# ===========================================================================

# Create a repo with two tags on the same commit
TMPDIR2="$(mktemp -d)"
# Update trap to clean up both
trap 'rm -rf "${TMPDIR}" "${TMPDIR2}"' EXIT

(
	cd "${TMPDIR2}"
	git init -q repo
	cd repo
	git config user.email "test@example.com"
	git config user.name "Test User"
	echo "content" > file.txt
	git add file.txt
	git commit -q -m "Initial commit"
	git tag -a -m "Alpha tag" v2.0.0a1
	git tag -a -m "Final tag" v2.0.0
)

echo ""
echo "--- Ambiguity without PLUGIN_TAG ---"

(
	cd "${TMPDIR2}/repo"
	# Without PLUGIN_TAG, multiple tags should cause failure
	unset PLUGIN_TAG 2>/dev/null || true
	if output="$("${GET_VERSION}" 2>&1)"; then
		fail "get_version should fail with ambiguous tags (got: ${output})"
	else
		pass "get_version fails with ambiguous tags without PLUGIN_TAG"
	fi
)

echo ""
echo "--- PLUGIN_TAG resolves ambiguity ---"

(
	cd "${TMPDIR2}/repo"
	output="$(PLUGIN_TAG="v2.0.0a1" "${GET_VERSION}" 2>&1)"
	assert_eq "PLUGIN_TAG=v2.0.0a1 yields alpha version" "2.0.0a1" "${output}"
)

(
	cd "${TMPDIR2}/repo"
	output="$(PLUGIN_TAG="v2.0.0" "${GET_VERSION}" 2>&1)"
	assert_eq "PLUGIN_TAG=v2.0.0 yields final version" "2.0.0" "${output}"
)

echo ""
echo "--- PLUGIN_TAG validation ---"

(
	cd "${TMPDIR2}/repo"
	if PLUGIN_TAG="invalid-tag" "${GET_VERSION}" > /dev/null 2>&1; then
		fail "get_version should reject invalid PLUGIN_TAG format"
	else
		pass "get_version rejects invalid PLUGIN_TAG format"
	fi
)

(
	cd "${TMPDIR2}/repo"
	if PLUGIN_TAG="v9.9.9" "${GET_VERSION}" > /dev/null 2>&1; then
		fail "get_version should reject PLUGIN_TAG that doesn't exist"
	else
		pass "get_version rejects non-existent PLUGIN_TAG"
	fi
)

# ===========================================================================
echo ""
echo "=== Test Suite: Failure paths ==="
# ===========================================================================

echo ""
echo "--- generate_debian_changelog.sh failure cases ---"

# Non-existent tag
(
	cd "${TMPDIR}/repo"
	if "${GENERATE_CHANGELOG}" v99.99.99 > /dev/null 2>&1; then
		fail "changelog should fail for non-existent tag"
	else
		pass "changelog fails for non-existent tag"
	fi
)

# Invalid tag format
if "${GENERATE_CHANGELOG}" "not-a-tag" > /dev/null 2>&1; then
	fail "changelog should reject invalid tag format"
else
	pass "changelog rejects invalid tag format"
fi

echo ""
echo "--- generate_release_tag dry-run failure cases ---"

# Missing tag in RELEASENOTES
(
	cd "${TMPDIR}/repo"
	git checkout -q v1.22.x
	# Dry-run with a tag whose version isn't in RELEASENOTES.md
	echo "# fake notes" > RELEASENOTES.md
	git add RELEASENOTES.md
	git commit -q -m "fake notes"
	if "${GENERATE_TAG}" --ref HEAD --tag v1.22.1 --dry-run > /dev/null 2>&1; then
		fail "generate_release_tag dry-run should fail without RELEASENOTES mention"
	else
		pass "generate_release_tag dry-run fails without RELEASENOTES mention"
	fi

	# A final tag must not match a longer alpha heading by substring.
	echo "# v1.22.1a1 (test)" > RELEASENOTES.md
	git add RELEASENOTES.md
	git commit -q -m "alpha-only notes"
	if "${GENERATE_TAG}" --ref HEAD --tag v1.22.1 --dry-run > /dev/null 2>&1; then
		fail "final tag should not match an alpha RELEASENOTES heading"
	else
		pass "final tag does not match an alpha RELEASENOTES heading"
	fi
	if "${GENERATE_TAG}" --ref HEAD --tag v1.22.1a1 --dry-run > /dev/null 2>&1; then
		pass "alpha tag matches its exact RELEASENOTES heading"
	else
		fail "alpha tag should match its exact RELEASENOTES heading"
	fi
)

# Wrong branch
(
	cd "${TMPDIR}/repo"
	git checkout -q v1.21.x
	if "${GENERATE_TAG}" --ref HEAD --tag v1.22.1 --dry-run > /dev/null 2>&1; then
		fail "generate_release_tag dry-run should fail on wrong branch"
	else
		pass "generate_release_tag dry-run fails on wrong branch"
	fi
)

# Existing tag
(
	cd "${TMPDIR}/repo"
	git checkout -q v1.22.x
	if "${GENERATE_TAG}" --ref HEAD --tag v1.22.0 --dry-run > /dev/null 2>&1; then
		fail "generate_release_tag dry-run should fail for existing tag"
	else
		pass "generate_release_tag dry-run fails for existing tag"
	fi
)

# Invalid tag format
(
	cd "${TMPDIR}/repo"
	if "${GENERATE_TAG}" --ref HEAD --tag "invalid" --dry-run > /dev/null 2>&1; then
		fail "generate_release_tag dry-run should reject invalid format"
	else
		pass "generate_release_tag dry-run rejects invalid format"
	fi
)

echo ""
echo "--- get_version.sh .release_version fallback ---"

(
	cd "${TMPDIR}"
	mkdir -p release_test
	cd release_test
	echo "1.22.0a1" > .release_version
	output="$("${GET_VERSION}")"
	assert_eq ".release_version is honored" "1.22.0a1" "${output}"
	rm -rf "${TMPDIR}/release_test"
)

# ===========================================================================
echo ""
echo "================================================================"
printf 'Results: %d tests run, %d passed, %d failed\n' \
	"${TESTS_RUN}" "${TESTS_PASSED}" "${TESTS_FAILED}"
echo "================================================================"

if [[ ${TESTS_FAILED} -gt 0 ]]; then
	exit 1
fi
exit 0
