#!/usr/bin/env python3
"""What happens to the Pratt voice as the base (and so n = (note+1) x base) grows.

Exact integer polynomials f_n, numpy roots, and the voice's harmonic weights
k^-roll * |H_n(i xi k)|. Prints (1) polynomial growth and pole stability by prime range,
(2) spectral tilt by base, (3) key-to-key variation by base at equal overall brightness.
Usage: python3 pratt_base_probe.py [prime_limit]   (default 100000; 1048576 takes a minute)
Needs numpy only. Results are summarised in ../docs/high-base.md.
"""
import sys
import numpy as np

LIMIT = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
sys.setrecursionlimit(10000)


def factor(n):
    out, p = [], 2
    while p * p <= n:
        while n % p == 0:
            out.append(p)
            n //= p
        p += 1 if p == 2 else 2
    if n > 1:
        out.append(n)
    return out


P = {1: [1], 2: [0, 1]}


def mul(a, b):
    c = [0] * (len(a) + len(b) - 1)
    for i, x in enumerate(a):
        for j, y in enumerate(b):
            c[i + j] += x * y
    return c


def prime_poly(p):
    if p in P:
        return P[p]
    r = list(poly(p - 1))
    r[0] += 1
    P[p] = r
    return r


def poly(n):
    r = [1]
    for p in factor(n):
        r = mul(r, prime_poly(p))
    return r


def H(n, y):
    x = 2 + 1j * y
    return n / sum(c * x ** i for i, c in enumerate(poly(n)))


def growth_and_stability():
    primes = [p for p in range(3, LIMIT + 1) if len(factor(p)) == 1]
    edges = [3, 128, 1024, 8192, 65536, 262144, 1 << 20, 1 << 22]
    print("polynomial growth and pole stability (stable iff Re(t) < 2; margin = 2 - Re(t))")
    for lo, hi in zip(edges, edges[1:]):
        if lo > LIMIT:
            break
        worst, at, deg, coef = -1e9, None, 0, 0
        for p in primes:
            if p < lo or p > hi:
                continue
            c = prime_poly(p)
            deg, coef = max(deg, len(c) - 1), max(coef, max(abs(x) for x in c))
            m = np.roots(c[::-1]).real.max()
            if m > worst:
                worst, at = m, p
        if at is not None:
            print(f"  p in ({lo},{hi}]: max degree {deg}, max |coef| {coef}, worst Re(t-2) {worst - 2:.4f} at p={at}")


def tilt(note=60, xi=0.16, roll=1.0, harmonics=56):
    print(f"\nspectral tilt, note {note}, xi {xi}, roll {roll}: dB re harmonic 1")
    for base in (1, 2, 7, 16, 64, 97, 257, 1009, 1024, 4099, 12347, 65537):
        n = (note + 1) * base
        a = np.array([k ** -roll * abs(H(n, xi * k)) for k in range(1, harmonics + 1)])
        db = 20 * np.log10(a / a[0])
        centroid = (a * np.arange(1, harmonics + 1)).sum() / a.sum()
        print(f"  base {base:>6} n {n:>8} deg {len(poly(n)) - 1:>2}: h2 {db[1]:6.1f} h4 {db[3]:6.1f} "
              f"h8 {db[7]:6.1f} h16 {db[15]:7.1f} centroid {centroid:5.2f}")


def variation(harmonics=40):
    ks = np.arange(1, harmonics + 1)

    def centroids(base, xi):
        out = []
        for note in range(36, 97):
            a = np.array([abs(H((note + 1) * base, xi * k)) / k for k in ks])
            out.append((a * ks).sum() / a.sum())
        return np.array(out)

    print("\nkey-to-key variation; xi re-scaled per base so the median centroid is 3.5")
    for base in (1, 2, 16, 64, 97, 1009, 1024, 4099, 12347, 65537):
        lo, hi = 0.002, 1.0
        for _ in range(18):
            mid = (lo * hi) ** 0.5
            lo, hi = (mid, hi) if np.median(centroids(base, mid)) > 3.5 else (lo, mid)
        xi = (lo * hi) ** 0.5
        c = centroids(base, xi)
        print(f"  base {base:>6}: xi {xi:6.4f} centroid {c.min():.2f}..{c.max():.2f} std {c.std():.3f} "
              f"mean neighbour jump {np.abs(np.diff(c)).mean():.3f}")


if __name__ == "__main__":
    growth_and_stability()
    tilt()
    variation()
