---
paths: "**/*.{cpp,h,hpp}"
description: Modern C++20 patterns for embedded Arduino development
---
# C++20 Patterns for Embedded Development

This project uses C++20. Apply modern idioms while respecting embedded constraints.

## Prefer Modern Constructs

| Legacy Pattern                         | Modern Alternative                         |
| -------------------------------------- | ------------------------------------------ |
| Fixed C arrays `uint8_t arr[N]`        | `std::array<uint8_t, N>`                   |
| Raw pointer + length                   | `std::span<uint8_t>`                       |
| Manual array loops                     | `std::ranges::equal()`, range-based for   |
| `#define CONSTANT 42`                  | `constexpr size_t CONSTANT = 42;`          |
| Manual swap                            | `std::swap(a, b)`                          |
| C-style casts `(int)x`                 | `static_cast<int>(x)`                      |
| `NULL`                                 | `nullptr`                                  |
| Output parameters                      | Return values (move semantics)             |

## Modern Attributes

- `[[nodiscard]]` - On functions where ignoring return is likely a bug
- `const` - For variables unchanged after initialization

## Exceptions to Modern Patterns

Legacy patterns acceptable when:
- **ISR context**: `volatile` for ISR↔main shared variables
- **Hardware registers**: Raw pointers for memory-mapped I/O
- **Arduino API**: Some functions require C-style arrays
- **Hot paths**: Fixed arrays to avoid heap in tight loops

## Callbacks

- Bluefruit requires C-style function pointers; bridge to methods through a global instance pointer (`g_bleManager`).
