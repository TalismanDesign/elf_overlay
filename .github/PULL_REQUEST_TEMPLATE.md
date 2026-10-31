## Summary

What does this change do, and why?

## Changes

-

## Testing

How did you verify the change? Check the boxes that apply.

- [ ] `ctest` passes with `-DELF_OVERLAY_WERROR=ON`
- [ ] Passes under `-DELF_OVERLAY_ASAN=ON`
- [ ] `make -C examples/host_pipeline check` passes (if the tools changed)
- [ ] ESP-IDF component build succeeds (if the device path changed)
- [ ] `clang-format` leaves the tree unchanged

## Notes

Anything reviewers should know: blob-format changes, ISA-specific behavior, or
follow-up work. Update `CHANGELOG.md` under `## Unreleased` for user-visible
changes.
