# 0011. `/signoff` for stacked pull requests

- Status: Proposed
- Date: 2026-11-11
- Amends: [ADR 0003](0003-signoff-by-comment.md) (how the merge is made)

The same change as the server's
[ADR 0024](https://github.com/nolsen311/dreadovania-server/blob/main/docs/adr/0024-signoff-stacked-pull-requests.md),
which has the full reasoning; this repository runs a copy of the same workflow.

## Context

GitHub refuses its merge endpoint (`PUT /pulls/{n}/merge`) for every pull
request in a stack, the bottom one included: "Merging stacked PRs via this
endpoint is not supported. Use the asynchronous merge endpoint instead." The
asynchronous endpoint (`PUT /pulls/{n}/merge-async`) is required for stacks, but
for a stacked PR it also merges the open PRs below it, and keeps a custom commit
message only for direct merges.

## Decision

1. The *Sign off and merge* workflow merges through the asynchronous endpoint,
   with `merge_action: direct_merge`, `merge_method: squash`, `bypass_rules:
   false` and the inspected head `sha`, and waits for the result.
2. `/signoff` still only merges a PR that targets the default branch: in a stack,
   the bottom one, which has nothing below it. A `/signoff` higher up is refused,
   so no commit reaches `main` without the owner's sign-off.
3. Stacks are signed off from the bottom up, one `/signoff` per PR, changing the
   next PR's base to the default branch in between.
4. After merging, the workflow checks that the new commit on the default branch
   carries the owner's `Signed-off-by:` line, and says so if it doesn't.

Everything else in ADR 0003 stands.

## Consequences

- `/signoff` works for stacked PRs, one at a time from the bottom.
- It was first used for real on the server (dreadovania-server#65), which merged
  with the sign-off in place.
