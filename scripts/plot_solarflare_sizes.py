"""Parse a completed four- or five-size sweep using only that log.
Usage: python3 scripts/plot_solarflare_sizes.py solarflare_latency_TIMESTAMP.txt
Requires Matplotlib. Raw benchmark output is never modified.
"""
import csv
from pathlib import Path
import re
import sys


def parse_sweep(text):
    pattern = re.compile(
        r'^(64|128|256|512|1024) ((?:[\d.]+ ){7}[\d.]+) '
        r'\[(\d+) new samples, (\d+) total samples, (\d+) seconds\]$', re.M)
    if 'No new responses' in text:
        raise ValueError('Sweep contains stalled intervals; inspect the raw log')
    runs = {size: [] for size in (64, 128, 256, 512, 1024)}
    for match in pattern.finditer(text):
        size, values, new, total, seconds = match.groups()
        values = [float(v) for v in values.split()]
        runs[int(size)].append((int(seconds), int(new), int(total), values))
    result = []
    for size, rows in runs.items():
        if size == 64 and not rows:
            continue  # Older sweeps measured only 128..1024 bytes.
        if [r[0] for r in rows] != list(range(1, 31)):
            raise ValueError(f'{size}: expected exactly 30 measurement rows')
        previous = 0
        for _, new, total, _ in rows:
            if new <= 0 or total != previous + new:
                raise ValueError(f'{size}: invalid sample counts')
            previous = total
        _, _, total, values = rows[-1]
        result.append([size, values[0], values[2], values[3], total])
    return result


def main():
    log = Path(sys.argv[1]).resolve()
    results = parse_sweep(log.read_text())
    target = log.with_suffix('.csv')
    with target.open('w') as f:
        writer = csv.writer(f)
        writer.writerow(['request_bytes', 'median_us', 'p99_us', 'p99_9_us', 'samples'])
        writer.writerows(results)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(8, 5), layout='constrained')
    for column, label in [(1, 'Median'), (2, 'p99'), (3, 'p99.9')]:
        ax.plot([r[0] for r in results], [r[column] for r in results],
                marker='o', label=label)
    ax.set_xscale('log', base=2)
    ax.set_xticks([r[0] for r in results], [str(r[0]) for r in results])
    ax.set_ylim(bottom=0)
    ax.set_xlabel('Request size (bytes)')
    ax.set_ylabel('RPC round-trip latency (µs)')
    ax.set_title('Solarflare two-port DAC · 8-byte responses\n'
                 '5 s warmup + 30 s measurement · one outstanding RPC')
    ax.legend()
    ax.grid(alpha=0.2)
    fig.supxlabel('One run per size; all points from this log', fontsize=9)
    for extension in ['png', 'svg']:
        fig.savefig(log.with_suffix('.' + extension), dpi=180)
    print(f'Summary: {target}\nPlot: {log.with_suffix(".png")}')


if __name__ == '__main__':
    main()
