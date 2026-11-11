#!/bin/sh
# Usage: coverage_table.sh <lcov-file> [min-line-percent]
# Prints per-file line and branch coverage for src/ and include/ plus a TOTAL
# row. Exits 1 when the total line percent is below min-line-percent.
[ -f "$1" ] || { echo "usage: $0 <lcov-file> [min-line-percent]" >&2; exit 2; }
awk -v min="${2:-0}" -v pwd="$PWD/" '
function pct(h, f) { return f > 0 ? sprintf("%.1f", 100 * h / f) : "-" }
function row(name, lh, lf, bh, bf) {
  printf "%-48s %6d %6d %6s %6d %6d %6s\n", name, lh, lf, pct(lh, lf), bh, bf, pct(bh, bf)
}
/^SF:/ { f = substr($0, 4); if (index(f, pwd) == 1) f = substr(f, length(pwd) + 1); lf = lh = bf = bh = 0; next }
/^LF:/ { lf = substr($0, 4) + 0; next }
/^LH:/ { lh = substr($0, 4) + 0; next }
/^BRF:/ { bf = substr($0, 5) + 0; next }
/^BRH:/ { bh = substr($0, 5) + 0; next }
/^end_of_record/ {
  if (f ~ /^(src|include)\//) {
    if (!(f in seen)) { seen[f] = 1; names[++n] = f }
    LF[f] += lf; LH[f] += lh; BF[f] += bf; BH[f] += bh
  }
  next
}
END {
  printf "%-48s %6s %6s %6s %6s %6s %6s\n", "File", "LnHit", "LnTot", "Ln%", "BrHit", "BrTot", "Br%"
  for (i = 1; i <= n; i++) {
    k = names[i]
    row(k, LH[k], LF[k], BH[k], BF[k])
    tlh += LH[k]; tlf += LF[k]; tbh += BH[k]; tbf += BF[k]
  }
  row("TOTAL", tlh, tlf, tbh, tbf)
  if (tlf > 0 ? 100 * tlh / tlf < min : min > 0) exit 1
}' "$1"
