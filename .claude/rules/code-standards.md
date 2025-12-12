---
description: Code quality standards and prohibited patterns
---
# Code Standards

## Prohibited in Source Code

**Never include planning artifacts:**
- Work package identifiers (WP1, WP2, etc.)
- Phase numbers (Phase 1, Phase 2, etc.)
- ADR references
- Sprint/iteration identifiers
- Migration plan references
- "TODO: implement in WP3" style comments

Source code should be self-documenting and timeless. Planning artifacts belong in `docs/`.

## Acceptable Practices

- Version numbers in headers (`@version 2.0.0`)
- Feature descriptions without planning context
- Technical TODOs (`// TODO: optimize buffer allocation`)
- GitHub issue references (`// Fixes #123`)

## Development Notes

- Serial output: 115200 baud
- BLE service: Nordic UART Service (NUS)
- Role configuration: `SET_ROLE:PRIMARY` or `SET_ROLE:SECONDARY`
- Build flags: `-DCFG_DEBUG=0 -DNRF52840_XXAA -std=gnu++20`
