---
name: review-pr
description: Perform a structured code review of an E2SAR GitHub PR and post inline + summary comments
user-invocable: true
argument-hint: <PR-number>
allowed-tools: Bash, Read, Glob, Grep
---

Perform a thorough, structured code review of E2SAR pull request `$ARGUMENTS` and post actionable GitHub comments.

## Steps

### 1. Fetch PR metadata and diff

```bash
gh pr view $ARGUMENTS --json number,title,body,author,baseRefName,headRefName
gh repo view --json nameWithOwner --jq '.nameWithOwner'   # capture as REPO
gh api repos/REPO/pulls/$ARGUMENTS/files --jq '.[].filename'
```

Display a summary to the user: PR title, author, base branch, files changed.

To read the actual changed lines for a file, use the per-file patch from the API — **do not parse `gh pr diff` with awk**, as range patterns silently drop content when the next `diff --git` header is adjacent:

```bash
# All patches at once (good for small PRs)
gh api repos/REPO/pulls/$ARGUMENTS/files --jq '.[] | {path: .filename, patch: .patch}'

# One file at a time
gh api repos/REPO/pulls/$ARGUMENTS/files \
  --jq '.[] | select(.filename == "path/to/file.cpp") | .patch'
```

### 2. Categorize changed files

Group the changed files by type and apply the matching checklist in Step 3:

| Pattern | Checklist |
|---------|-----------|
| `include/*.hpp`, `src/*.cpp` | C++ style + docs |
| `src/pybind/py_*.cpp` | pybind11 bindings |
| `test/py_test/test_*.py` | pytest conventions |
| `test/*.cpp` | Boost.Test conventions |
| `meson.build` | Build system |
| `RELEASE-NOTES.md`, `VERSION.txt` | Version hygiene |

For context on project conventions, read the relevant reference files as needed:
- `include/e2sarError.hpp` — `E2SARErrorc` enum and `E2SARErrorInfo`
- `include/e2sarURI.hpp` — naming/docs conventions
- `include/e2sarDPSegmenter.hpp` — Flags struct, thread state, atomics
- `include/e2sarCP.hpp` — gRPC `result<T>` pattern
- `src/pybind/py_e2sarDP.cpp` — pybind11 patterns
- `test/py_test/test_b2b_DP.py` — pytest helper/marker conventions
- `test/py_test/pytest.ini` — official list of valid pytest markers

### 3. Apply review checklists

#### C++ checklist (`include/*.hpp`, `src/*.cpp`)

**Naming conventions**
- Classes: PascalCase (`Segmenter`, `LBManager`)
- Methods: camelCase (`openAndStart()`, `sendEvent()`); private helpers prefixed with `_` (`_open()`, `_threadBody()`)
- Getters: `get_` prefix; setters: `set_` prefix; boolean queries: `has_` / `is` prefix
- Variables: camelCase; constants/constexpr: UPPER_SNAKE_CASE
- Type aliases: PascalCase with `_t` suffix (`EventNum_t`, `UnixTimeNano_t`)

**Documentation**
- All public class methods in headers must have a Doxygen `/** ... */` block with `@param` per parameter and `@return`
- Constructors must document every parameter
- `noexcept` methods must be declared as such in the signature
- Deleted copy constructors/assignment operators must have a brief `/** Don't want to copy... */` comment
- Structs with multiple fields should have a descriptive block listing each field

**Error handling**
- Constructors throw `E2SARException` on failure — must not silently swallow errors
- Methods returning `result<T>` must be marked `noexcept`
- Callers of `result<T>` must check `has_error()` before `.value()`; direct `.value()` without check is a bug
- New error codes must be added to `E2SARErrorc` enum in `include/e2sarError.hpp`; never return raw `-1`

**Memory and thread safety**
- Shared mutable state accessed from multiple threads must use `std::atomic<>` or a mutex
- New exclusively-owned class members should use `std::unique_ptr`; shared ownership uses `std::shared_ptr`
- Classes with thread ownership must delete copy constructor and copy assignment
- Large objects passed into threads should use `std::move`; never pass by copy if avoidable
- Raw `new` without a corresponding `delete` or RAII wrapper is a memory leak; use `std::unique_ptr<T>` or pool allocator

**Header structure**
- Header guards: `#ifndef E2SAR<NAME>HPP / #define E2SAR<NAME>HPP`
- Include order: standard library → third-party (Boost, gRPC) → project headers
- `using namespace` declarations inside `namespace e2sar {}` only; never at global scope in headers
- `"string"s` literals require `using namespace std::string_literals;` in scope

#### pybind11 checklist (`src/pybind/py_*.cpp`)

- Each bound method must have a short docstring as the last string argument to `.def()`
- Python-side names use snake_case (`get_instance_token`, `set_instance_token`)
- Enum values use lowercase (`.value("admin", ...)`)
- Any binding that calls a Python callable from a C++ thread must use GIL management (`py::gil_scoped_acquire`)
- Buffer/numpy methods must use `py::buffer_info` and cast through `buf_info.ptr`

#### pytest checklist (`test/py_test/test_*.py`)

- Every test function must start with a `"""Test <thing being tested>."""` docstring
- Each test must be decorated with exactly one marker from `pytest.ini`: `@pytest.mark.unit`, `@pytest.mark.b2b`, `@pytest.mark.cp`, or `@pytest.mark.lb-b2b`
- `result<T>` return values must be checked: `assert res.has_error() is False, f"{res.error().message}"`
- b2b tests using ports must account for TIME_WAIT; use different port numbers across tests or add socket options
- Helper functions (not tests) must not have a `test_` prefix

#### Boost.Test checklist (`test/*.cpp`)

- Each test case registered with `BOOST_AUTO_TEST_CASE` should test one logical thing
- Failures must use `BOOST_CHECK_*` / `BOOST_REQUIRE_*` macros, not raw `assert`
- New test executables must be registered in `test/meson.build` with `suite: 'unit'` or `suite: 'live'`

#### Meson build checklist (`meson.build`)

- New source files must be listed in the appropriate `meson.build` `sources:` array (silently excluded otherwise)
- New test executables must be registered with `test('Name', exe, suite: 'unit|live')`
- New dependencies must follow `dependency('name', version: '>=x.y.z')` pattern
- Optional features (liburing, NUMA) must use `compiler.has_header()` / `compiler.compiles()` guards
- `link_args: linker_flags` must be included for all executables/libraries that link e2sar

#### Version hygiene (`RELEASE-NOTES.md`, `VERSION.txt`)

- `VERSION.txt` must be updated if the PR introduces a new release
- `RELEASE-NOTES.md` entry must match the version in `VERSION.txt`
- Alpha/beta suffixes (`a1`, `b1`) must be removed before a final release commit

#### Secrets and usernames

- Files must not contain secrets that look real - random strings should be flagged as potential secrets.
- Strings that look like user names should also be flagged. 
- Pay special attention to strings that look like 'ejfats://token@host:port/...' or 'ejfat://token@host:port/...' or mention EJFAT_URI. It is allowed to use the word 'token' or 'randomstring' or some variation of this to denote the secret token, however strings that look random likely represent real secrets and must be flagged. 

### 4. Post inline GitHub review comments

For each issue found, post an inline **brief** comment using the GitHub API. Determine the commit SHA and file path from the diff.

First, get the latest commit SHA on the PR head:
```bash
gh pr view $ARGUMENTS --json headRefOid --jq '.headRefOid'
```

Then post each inline comment:
```bash
gh api repos/{owner}/{repo} --method GET --jq '.full_name'
# Use the returned full_name as REPO below

gh api repos/REPO/pulls/$ARGUMENTS/comments \
  --method POST \
  --field body='COMMENT_TEXT' \
  --field commit_id='HEAD_SHA' \
  --field path='FILE_PATH' \
  --field line=LINE_NUMBER \
  --field side='RIGHT'
```

Get `REPO` once with:
```bash
gh repo view --json nameWithOwner --jq '.nameWithOwner'
```

Use this comment templates (adapt the specific details):

**C++ naming (private helper):**
> `parseFromString` should be `_parseFromString` — private helpers use the `_` prefix. See `Segmenter::_open()` for the convention.

**Missing Doxygen:**
> Public method `computeChecksum()` is missing a `/** ... */` Doxygen block. Add `@param` for each argument and `@return` describing the result.

**result<T> misuse:**
> `res.value()` is called without checking `res.has_error()` first. This will throw if the call fails. Wrap in `if (!res.has_error()) { ... }` or assert.

**noexcept missing:**
> This method returns `result<T>` but isn't marked `noexcept`. All `result<T>`-returning methods should be `noexcept` — exceptions are encoded in the result type.

**Thread safety:**
> This counter is incremented from multiple threads without synchronization. Use `std::atomic<uint64_t>` as done in `Segmenter::AtomicStats::errCnt`.

**Missing test marker:**
> This test function has no pytest marker. Add `@pytest.mark.unit` (or the appropriate category) so it's included in the right test suite.

**Binding docstring missing:**
> `.def("methodName", ...)` is missing a docstring. Add a brief description as the last string argument.

**Build file omission:**
> `new_module.cpp` is not listed in `src/meson.build`'s source list. It will be silently excluded from the build.

**Error code missing:**
> This failure path returns a raw value instead of `E2SARErrorInfo{E2SARErrorc::SocketError, "..."}`. Use the proper error type.

**Memory leak risk:**
> Raw `new` without a corresponding `delete` or RAII wrapper. Use `std::unique_ptr<T>` or the existing pool allocator.

### 5. Post a summary comment

After all inline comments, post one top-level PR comment with the overall assessment.
Use a heredoc to avoid quoting failures when the body contains single quotes or backticks:

```bash
gh pr comment $ARGUMENTS --body "$(cat <<'EOF'
SUMMARY
EOF
)"
```

The summary must include:
- **Overall verdict**: Approve / Request Changes / Comment
- **Blocking issues** (must fix before merge): numbered list, **briefly** describing each problem. 
- **Non-blocking suggestions** (style/docs): bulleted list **briefly** describing each suggestion
- **What looks good**: **brief** callouts of well-done sections
- Footer: `> Review generated by /review-pr skill — verify all inline comments before merging.`

## Important notes

- Only report issues that are actually present in the diff — do not flag pre-existing code outside the changed lines
- Distinguish blocking issues (correctness, memory safety, build breakage) from non-blocking ones (style, docs)
- If no issues are found in a category, state that explicitly in the summary rather than inventing feedback
- Always fetch the repo name dynamically with `gh repo view` — never hardcode it
- If `$ARGUMENTS` is empty or not a valid PR number, ask the user for the PR number before proceeding
- Be **brief**, only provide enough information to explain the problem or suggestion. Do not be conversational, instead use **brief** and to-the-point suggestions only.
