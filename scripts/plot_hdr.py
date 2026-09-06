#!/usr/bin/env python3
"""Plot complete HDR bin exports; never infer distributions from percentiles."""
import argparse
import csv
import math
from pathlib import Path


def read_histogram(path):
    meta, rows = {}, []
    with Path(path).open() as stream:
        for line in stream:
            if line.startswith('# '):
                key, value = line[2:].strip().split('=', 1)
                meta[key] = value
            else:
                rows.append(line)
    if meta.get('format') != 'erpc_hdr_v1':
        raise ValueError(f'{path}: unsupported histogram format')
    bins = []
    previous = -1.0
    for row in csv.DictReader(rows):
        low, high, count = float(row['lower_us']), float(row['upper_us']), int(row['count'])
        if not (math.isfinite(low) and math.isfinite(high) and 0 <= low <= high
                and low > previous and count > 0):
            raise ValueError(f'{path}: invalid or unordered histogram bin')
        bins.append((high, count))
        previous = high
    total = sum(count for _, count in bins)
    if not total or total != int(meta['total_count']):
        raise ValueError(f'{path}: histogram count mismatch')
    cumulative = 0
    x, cdf, ccdf = [], [], []
    for high, count in bins:
        cumulative += count
        x.append(high)
        cdf.append(cumulative / total)
        ccdf.append((total - cumulative) / total)
    return meta, x, cdf, ccdf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('histograms', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, default=Path('results/solarflare/distributions'))
    parser.add_argument('--labels', nargs='+', help='One label per input; keep trials separate')
    args = parser.parse_args()
    if args.labels and len(args.labels) != len(args.histograms):
        parser.error('--labels must match the number of inputs')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5), constrained_layout=True)
    for index, path in enumerate(args.histograms):
        meta, x, cdf, ccdf = read_histogram(path)
        label = args.labels[index] if args.labels else path.stem
        label += f" (N={int(meta['total_count']):,})"
        axes[0].step([0] + x, [0] + cdf, where='post', label=label)
        positive = [(t, p) for t, p in zip(x, ccdf) if p > 0]
        if positive:
            # Preserve the last nonzero plateau up to (but excluding) the
            # maximum bin bound, where the empirical survival becomes zero.
            positive.append((math.nextafter(x[-1], -math.inf), positive[-1][1]))
            if x[0] > 0:
                positive.insert(0, (0, 1.0))
            tx, ty = zip(*positive)
            axes[1].step(tx, ty, where='post', label=label)
    axes[0].set(ylabel='Fraction of RTT samples ≤ latency', ylim=(0, 1.01), title='CDF')
    axes[1].set(ylabel='Fraction of RTT samples > latency', yscale='log', title='Tail CCDF (zero omitted)')
    for ax in axes:
        ax.set_xlabel('Round-trip latency (µs; HDR bin upper bound)')
        ax.grid(True, which='both', alpha=.25)
        ax.legend(fontsize=7)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for suffix in ('.png', '.svg'):
        fig.savefig(str(args.output) + suffix, dpi=180)
    plt.close(fig)


if __name__ == '__main__':
    main()
