# x86-64 UEFI Slowdown Bisect - 2026-03-27

## Scope

- Good baseline: `main` at `aad79322586070a60d4121c28f6d9177e777c5d1`
- Bad baseline: `dev/vga` at `8c4fdc47683f2ad76179e035621427c066b62363`
- Symptom: severe slowdown on x86-64 UEFI
- Repository state after bisect: `git bisect reset` executed, branch restored to `dev/vga`

## Tested commits

| Commit | Subject | Result |
| --- | --- | --- |
| `1974cbff27723d90fee5a59318c395d1f68a4b9d` | Add themed corner style and auto rounded buttons | good |
| `6167d170d66517b2fa2f46d5efc4483a987add3c` | Remove lock role debug wrappers | good |
| `fd9634a3c842790f6fbd1334910c9d962c8df1a7` | Add lock class order diagnostics | skip |
| `e190fc860d8c798adf95f0cfda9b262178955f64` | Add deadlock monitor hooks | skip |
| `3e7c1532af4e5f23597d70a5b6a70743244644d7` | Add desktop pipeline trace instrumentation | skip |
| `990e6e395a78fd4a819af998eb3a367d8affc3d0` | Fix desktop window focus handling | skip |
| `c72d459a227b7043bd7e450aa3eed103e450dfc7` | Add deadlock diagnostics and fail-fast | good |
| `a9d02e9f082dcb37825bde2f73f3d4aaabfd5eb0` | Isolate scheduler-owned task state | skip |

## Result

The bisect could not identify a single first bad commit because only skipped commits remained.

Possible first bad commits:

1. `fd9634a3c842790f6fbd1334910c9d962c8df1a7` - Add lock class order diagnostics
2. `3e7c1532af4e5f23597d70a5b6a70743244644d7` - Add desktop pipeline trace instrumentation
3. `990e6e395a78fd4a819af998eb3a367d8affc3d0` - Fix desktop window focus handling
4. `a9d02e9f082dcb37825bde2f73f3d4aaabfd5eb0` - Isolate scheduler-owned task state
5. `8c4fdc47683f2ad76179e035621427c066b62363` - Fix x86-64 packing regressions

## Notes

- `fd9634a3c842790f6fbd1334910c9d962c8df1a7` was skipped because of an early boot crash fixed later.
- The other skipped commits were not testable for the targeted slowdown in the available state.
