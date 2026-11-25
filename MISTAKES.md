# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## voxmod: bypass stopped muting during the three-way blend (2026-11-25)
- **What happened:** `testBypassIsTransparent` failed after adding Conv Mix; the bypassed plugin still passed the ring and vocoder engines.
- **Root cause:** the new `legacyAmount` was derived only from `1 - convAmount`, dropping the existing `bypassed ? 0 : ...` condition when the amounts were refactored.
- **Prevention:** derive every blend branch from the bypass flag in one place, and rerun the whole plugin suite (including bypass) after changing any amount expression.

## voxmod: direction test for commutative frame multiplication (2026-11-25)
- **What happened:** a new test asserted that swapping the source and IR pairs changes the convolution output; it failed because the outputs matched.
- **Root cause:** frequency-domain frame multiplication is commutative, so cross-synthesis has no source/IR direction; the test encoded an assumption from linear convolution with a fixed IR.
- **Prevention:** derive the algebra before writing an observable-effect test; prove the routing contract with silence on one pair instead of swapping symmetric operands.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
