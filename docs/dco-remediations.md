# DCO remediations

Commits on `main` that landed without a `Signed-off-by:` line, and the later
commit in which the maintainer signed off on them. History on `main` is never
rewritten, so a missing sign-off is fixed by a follow-up commit listed here.

A remediation takes effect when the commit that adds its entry is merged with
the maintainer's `Signed-off-by:` line. That sign-off certifies the Developer
Certificate of Origin 1.1 (see [`CONTRIBUTING.md`](../CONTRIBUTING.md)) for each
commit listed in the entry, as if it had been signed off at merge.

## 2026-11-07

The initial commit, pushed directly to the empty repository before branch
protection and the DCO workflow existed.

| Commit | Subject |
| --- | --- |
| `c18acdf80421badbf4bc3adb7d60fe680fbf29e6` | Initial commit: GPL-3.0 license, README, plan and ruleset |

## 2026-11-11

The four stacked pull requests for several servers (ADR 0010),
[#20](https://github.com/nolsen311/dreadovania-nx/pull/20) to
[#23](https://github.com/nolsen311/dreadovania-nx/pull/23). They were merged from
the GitHub web interface, whose commit message box for stacked pull requests
doesn't show the existing text clearly, and the `Signed-off-by:` line was left out.

| Commit | Subject |
| --- | --- |
| `92d4f9758b96ad2cb6b4ba3c635d1d840c65bb9d` | Several servers, 1/4: ADR 0010, picked by the network the Switch is on (#20) |
| `59a634cf3413f7d8f15d9f9ac18b237231cb0dd0` | Several servers, 2/4: keep a list of paired servers (#21) |
| `5429398d9ce88c44bc5125c3d41a4187dc6e81df` | Several servers, 3/4: pick the server by the network the Switch is on (#22) |
| `763681ea65943a848779a26bba0bf64f6167bce8` | Several servers, 4/4: the Servers screen (#23) |
