"""Plot the recorded server_rate preflight sweep; requires Matplotlib."""
import csv
from pathlib import Path
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parent
source = root.parent.parent / 'server_rate_preflight_20260905_162900_109781.txt'
text = source.read_text()
rows = [dict(re.findall(r'([\w.]+)=([\d.]+)', line))
        for line in text.splitlines() if line.startswith('PREFLIGHT_SUMMARY ')]
assert [int(r['window']) for r in rows] == [1, 2, 4, 8, 16, 32]
intervals = [line.split() for line in text.splitlines()
             if line.startswith('PREFLIGHT_INTERVAL ') and not line.startswith('PREFLIGHT_INTERVAL window')]
assert len(intervals) == 180
for row in rows:
    completed = [int(i[2]) for i in intervals if i[1] == row['window']]
    assert len(completed) == 30 and min(completed) > 0
    assert sum(completed) == int(row['completed'])
    assert int(row['completed']) - int(row['latency_samples']) == int(row['window'])
with (root / 'server-rate-sweep.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader(); writer.writerows(rows)
windows = [int(r['window']) for r in rows]
fig, axes = plt.subplots(1, 2, figsize=(11, 4.5), layout='constrained')
fig.suptitle('Solarflare eRPC concurrency sweep · 64-byte requests / 8-byte replies\n'
             'One client worker + one server worker · 5 s warmup + 30 s measurement', fontsize=12)
axes[0].plot(windows, [float(r['mrps']) for r in rows], 'o-', color='#2563eb')
axes[0].set_ylabel('Completed RPCs (millions/s)')
axes[0].set_ylim(0, 0.9)
axes[0].set_title('Throughput plateaus near window 8')
for key, label in [('median_us', 'Median'), ('p99_us', 'p99'), ('p99.9_us', 'p99.9')]:
    axes[1].plot(windows, [float(r[key]) for r in rows], 'o-', label=label)
axes[1].set_ylabel('Round-trip latency (µs)')
axes[1].set_ylim(bottom=0)
axes[1].set_title('More concurrency increases latency')
axes[1].legend()
for ax in axes:
    ax.set_xscale('log', base=2)
    ax.set_xticks(windows, [str(w) for w in windows])
    ax.set_xlabel('Outstanding RPCs (window)')
    ax.grid(alpha=0.2)
    ax.spines[['top', 'right']].set_visible(False)
fig.supxlabel('One run per window; final post-warmup summaries, not repeated-run averages.', fontsize=9)
fig.savefig(root / 'server-rate-sweep.png', dpi=180)
fig.savefig(root / 'server-rate-sweep.svg')
print('Validated all 180 intervals and six summary counts; wrote CSV, PNG, SVG.')
