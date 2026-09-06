#!/usr/bin/env python3
"""Validate and reproduce the 2026-09-05 repeated Solarflare measurements."""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics as st
import tarfile
from plot_hdr import read_histogram
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'results/solarflare/repeated-20260905'
manifest = json.loads((OUT / 'manifest.json').read_text())
records = []
curves = {}
with tarfile.open(OUT / 'raw-logs.tar.gz') as archive:
    for entry in manifest:
        path = OUT / 'histograms' / Path(entry['source']).name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
        meta, x, cdf, ccdf = read_histogram(path)
        log = archive.extractfile(entry['log']).read().decode()
        assert 'No new responses' not in log and 'Aborted' not in log
        bench, size, window = meta['benchmark'], int(meta['request_bytes']), int(meta['window'])
        n = int(meta['total_count'])
        assert int(meta['response_bytes']) == 8
        assert int(meta['warmup_ms']) == 5000 and int(meta['measurement_ms']) == 30000
        def percentile(p):
            # HDR rounds the target rank to nearest integer, then returns bin upper bound.
            rank = max(1, int(p / 100 * n + .5))
            return next(t for t, fraction in zip(x, cdf) if round(fraction * n) >= rank)
        row = dict(benchmark=bench, request_bytes=size, window=window,
                   trial=path.stem, samples=n, median_us=percentile(50),
                   p99_us=percentile(99), p99_9_us=percentile(99.9),
                   p99_99_us=percentile(99.99), max_us=x[-1], mrps='', histogram=path.name)
        if bench == 'erpc_latency':
            lines = re.findall(r'^(\d+) ((?:[\d.]+ ){7}[\d.]+) \[(\d+) new samples, (\d+) total samples, (\d+) seconds\]$',log,re.M)
            assert len(lines) == 30 and [int(a[4]) for a in lines] == list(range(1,31))
            assert all(int(a[0]) == size and int(a[2]) > 0 for a in lines)
            assert sum(int(a[2]) for a in lines) == n == int(lines[-1][3])
            values = list(map(float,lines[-1][1].split()))
            reported = [values[0],values[2],values[3]]
            row['trial'] = re.search(r'trial(\d+)', path.name)[1]
        elif bench == 'server_rate':
            summaries = [dict(re.findall(r'(\S+)=(\S+)', line)) for line in log.splitlines() if line.startswith('PREFLIGHT_SUMMARY')]
            match = [v for v in summaries if int(v['window']) == window]
            assert len(match) == 1
            v = match[0]
            intervals = [line.split() for line in log.splitlines() if line.startswith(f'PREFLIGHT_INTERVAL {window} ')]
            assert len(intervals) == 30 and all(int(a[2]) > 0 for a in intervals)
            assert sum(int(a[2]) for a in intervals) == int(v['completed'])
            assert int(v['latency_samples']) == n and int(v['completed']) - n == window
            assert abs(float(v['elapsed_s']) - 30) < .1
            row['mrps'] = float(v['mrps'])
            row['completed'] = int(v['completed'])
            row['elapsed_s'] = float(v['elapsed_s'])
            assert abs(row['completed'] / row['elapsed_s'] / 1e6 - row['mrps']) < 1e-6
            reported = [float(v[k]) for k in ('median_us','p99_us','p99.9_us')]
        else:
            assert bench == 'dpdk_pingpong' and size == 64 and window == 1
            lines = [a.split() for a in log.splitlines() if re.match(r'^64 [\d.]+ ',a)]
            assert len(lines) == 30 and int(lines[-1][-2]) == n
            assert [float(a[-1]) for a in lines] == list(range(1,31))
            counters = re.findall(r'Port \d+ RX=\d+ TX=\d+ missed=(\d+) RX_errors=(\d+) TX_errors=(\d+) RX_nombuf=(\d+)',log)
            assert len(counters) == 2 and all(int(v) == 0 for a in counters for v in a)
            reported = list(map(float,lines[-1][1:4]))
        assert all(abs(row[k]-v) < .051 for k,v in zip(('median_us','p99_us','p99_9_us'),reported)), path
        records.append(row)
        curves[path.name] = (x,cdf,ccdf)

assert len(records) == 46
for size in [64,128,256,512,1024]:
    assert len([r for r in records if r['benchmark']=='erpc_latency' and r['request_bytes']==size]) == 5
for window in [1,2,4,8,16,32]:
    assert len([r for r in records if r['benchmark']=='server_rate' and r['window']==window]) == 3
assert len([r for r in records if r['benchmark']=='dpdk_pingpong']) == 3
fields = list(dict.fromkeys(k for r in records for k in r))
with (OUT/'per-trial.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(records)

plt.rcParams.update({'font.size':10, 'axes.spines.top':False, 'axes.spines.right':False})
def save(fig,name):
    for suffix in ('png','svg'): fig.savefig(OUT/f'{name}.{suffix}',dpi=180)
    plt.close(fig)
def group(bench,key,value):
    return [r for r in records if r['benchmark']==bench and r[key]==value]
def errorplot(ax,groups,metric,label,color,offset=0):
    means=[st.mean(r[metric] for r in g) for g in groups]
    low=[m-min(r[metric] for r in g) for m,g in zip(means,groups)]
    high=[max(r[metric] for r in g)-m for m,g in zip(means,groups)]
    pos=[i+offset for i in range(len(groups))]
    ax.errorbar(pos,means,yerr=[low,high],fmt='o-',capsize=4,label=label,color=color)
    for p,g in zip(pos,groups):
        ax.scatter([p+(j-(len(g)-1)/2)*.025 for j in range(len(g))], [r[metric] for r in g],s=13,color=color,alpha=.5)
    ax.grid(alpha=.2)

fig,axes=plt.subplots(1,3,figsize=(15,4.7),layout='constrained')
sets=[group('dpdk_pingpong','request_bytes',64),group('erpc_latency','request_bytes',64)]
for metric,label,color in [('median_us','Median','C0'),('p99_us','p99','C1')]: errorplot(axes[0],sets,metric,label,color)
axes[0].set(xticks=[0,1],xticklabels=['DPDK (3 trials)','eRPC (5 trials)'],ylabel='RTT (µs)',title='64 B request / 8 B reply')
sizes=[64,128,256,512,1024]; sets=[group('erpc_latency','request_bytes',v) for v in sizes]
for metric,label,color in [('median_us','Median','C0'),('p99_us','p99','C1'),('p99_9_us','p99.9','C2')]: errorplot(axes[1],sets,metric,label,color)
axes[1].set(xticks=range(5),xticklabels=sizes,xlabel='Request bytes',ylabel='RTT (µs)',title='eRPC size sweep (5 trials each)')
windows=[1,2,4,8,16,32]; sets=[group('server_rate','window',v) for v in windows]
errorplot(axes[2],sets,'mrps','Completed RPCs/s','C0')
axes[2].set(xticks=range(6),xticklabels=windows,xlabel='Outstanding RPCs',ylabel='Million RPCs/s',title='Concurrency throughput (3 trials each)')
for ax in axes: ax.legend(fontsize=8)
fig.supxlabel('Points/lines: mean of per-trial statistics; error bars: min–max; small dots: individual trials (not confidence intervals)',fontsize=9)
save(fig,'repeatability')
fig,ax=plt.subplots(figsize=(8,4.5),layout='constrained')
for metric,label,color in [('median_us','Median','C0'),('p99_us','p99','C1'),('p99_9_us','p99.9','C2')]: errorplot(ax,sets,metric,label,color)
ax.set(xticks=range(6),xticklabels=windows,xlabel='Outstanding RPCs',ylabel='RTT (µs)',title='Concurrency latency: mean and min–max across 3 trials');ax.legend();save(fig,'concurrency-latency')

def distributions(groups,name,title):
    fig,axes=plt.subplots(1,2,figsize=(12,4.6),layout='constrained')
    for index,(label,rows) in enumerate(groups):
        for j,row in enumerate(rows):
            x,cdf,ccdf=curves[row['histogram']]
            axes[0].step(x,cdf,where='post',color=f'C{index}',alpha=.65,label=label if j==0 else None)
            positive=[(t,p) for t,p in zip(x,ccdf) if p>0]
            positive.append((math.nextafter(x[-1],-math.inf),positive[-1][1]))
            tx,ty=zip(*positive)
            axes[1].step(tx,ty,where='post',color=f'C{index}',alpha=.65,label=label if j==0 else None)
    axes[0].set(ylabel='P(RTT ≤ t)',ylim=(0,1.01),title='CDF')
    axes[1].set(ylabel='P(RTT > t)',yscale='log',title='Tail CCDF; zeros omitted')
    for ax in axes:
        ax.set(xscale='log',xlabel='RTT bin upper bound (µs; logarithmic scale)');ax.grid(alpha=.2,which='both');ax.legend(fontsize=8)
    fig.suptitle(title)
    fig.supxlabel('Each line is one trial; matching colors identify a condition. Histograms are not pooled.',fontsize=9)
    save(fig,name)
distributions([('DPDK 64 B (3 trials)',group('dpdk_pingpong','request_bytes',64)),('eRPC 64 B (5 trials)',group('erpc_latency','request_bytes',64))],'baseline-distributions','DPDK baseline versus eRPC, 64-byte requests / 8-byte replies')
distributions([(f'{v} B (5 trials)',group('erpc_latency','request_bytes',v)) for v in sizes],'size-distributions','eRPC request-size distributions')
distributions([(f'Window {v} (3 trials)',group('server_rate','window',v)) for v in [1,8,32]],'window-distributions','eRPC concurrency distributions, 64-byte requests / 8-byte replies')

# A frequency histogram provides the peak-and-tail view, unlike a cumulative plot.
fig,ax=plt.subplots(figsize=(9,4.5),layout='constrained')
for index,bench in enumerate(['dpdk_pingpong','erpc_latency']):
    for j,row in enumerate(group(bench,'request_bytes',64)):
        x,cdf,_=curves[row['histogram']]
        bins=[i*.1 for i in range(51,301)]
        mass=[0.0]*250
        prev=0
        for t,p in zip(x,cdf):
            slot=int(round(t*10))-51
            if 0<=slot<250: mass[slot]+=p-prev
            prev=p
        ax.step(bins,mass,where='mid',color=f'C{index}',alpha=.55,label=bench if j==0 else None)
ax.set(xlabel='RTT (µs)',ylabel='Fraction per 0.1 µs bin',title='64 B RTT histogram: central 5–30 µs view, each trial separate',xlim=(5,30));ax.legend();ax.grid(alpha=.2)
fig.supxlabel('HDR bin mass assigned to upper bounds; values above 30 µs omitted here but included in CDF/CCDF.',fontsize=9)
save(fig,'baseline-histogram')

def statcell(rows,key,digits=2):
    a=[r[key] for r in rows]
    return f'{st.mean(a):.{digits}f} [{min(a):.{digits}f}, {max(a):.{digits}f}]'
section='''## Historical results — repeated histogram measurements, 2026-09-05

Validated **five eRPC size-sweep trials**, **three concurrency sweeps**, and
**three DPDK baseline trials**: 46 complete measured distributions. Every run
used 5 seconds warmup and 30 seconds measurement, with 8-byte responses.
Histogram totals match the corresponding text logs; all required measurement
intervals completed. The three DPDK runs reported zero missed packets, RX/TX
errors, and RX allocation failures on both ports.

These are Phase 0 single-host Solarflare measurements. DPDK and eRPC have the
configuration differences described above; their RTT gap is not isolated
protocol overhead.

### Repeatability and error bars

Tables show **mean of per-trial statistics [minimum, maximum]**. Error bars show
the same observed between-trial range, not standard deviation or a confidence
interval. Each trial has equal weight. A zero-width median range means the
recorded medians agree at HDR precision; it does not prove zero timing variation.

![Repeatability: baseline, message sizes, throughput](results/solarflare/repeated-20260905/repeatability.png)

| 64-byte test | Trials | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) |
|---|---:|---:|---:|---:|
'''
for bench,label in [('dpdk_pingpong','DPDK ping-pong'),('erpc_latency','eRPC latency')]:
    rows=group(bench,'request_bytes',64)
    section+=f'| {label} | {len(rows)} | '+ ' | '.join(statcell(rows,k) for k in ['median_us','p99_us','p99_9_us'])+' |\n'
section+='\n### Request-size sweep: five trials per size\n\n| Request bytes | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) | Total measured samples |\n|---:|---:|---:|---:|---:|\n'
for size in sizes:
    rows=group('erpc_latency','request_bytes',size)
    section+=f'| {size} | '+' | '.join(statcell(rows,k) for k in ['median_us','p99_us','p99_9_us'])+f" | {sum(r['samples'] for r in rows):,} |\n"
section+='\n### Concurrency sweep: three trials per window\n\n| Window | Throughput (Mrps) | Median RTT (µs) | p99 RTT (µs) | p99.9 RTT (µs) |\n|---:|---:|---:|---:|---:|\n'
for window in windows:
    rows=group('server_rate','window',window)
    section+=f'| {window} | '+statcell(rows,'mrps',3)+' | '+' | '.join(statcell(rows,k) for k in ['median_us','p99_us','p99_9_us'])+' |\n'
section+='''
![Concurrency latency with trial ranges](results/solarflare/repeated-20260905/concurrency-latency.png)

Increasing concurrency initially raises throughput. Beyond window 8, the rate
changes little while RTT rises substantially. This is consistent with queueing
at a processing limit, but does not identify whether the limit is in the client,
server, PMD, or NIC. These repetitions characterize observed variation; they do
not rule out thermal or operating-system interference.

### Full histograms, CDFs, and tail CCDFs

Every curve below is one trial; colors group identical conditions. No trials
are pooled and no curves are reconstructed from a few percentile points.
The frequency histogram shows the peak; the CDF and CCDF retain the full range,
including rare slow samples. The CCDF uses log probability; distribution plots
also use log latency to keep the full tails visible. Zero CCDF values are
omitted. Far-tail estimates supported by only a few samples are unstable.

![64-byte frequency histograms](results/solarflare/repeated-20260905/baseline-histogram.png)

![DPDK versus eRPC CDF and CCDF](results/solarflare/repeated-20260905/baseline-distributions.png)

![All request sizes: CDF and CCDF](results/solarflare/repeated-20260905/size-distributions.png)

![Windows 1, 8, 32: CDF and CCDF](results/solarflare/repeated-20260905/window-distributions.png)

### Saved data and reproduction

- [Per-trial statistics](results/solarflare/repeated-20260905/per-trial.csv)
- [All 46 histogram CSVs](results/solarflare/repeated-20260905/histograms/)
- [Source filenames and histogram SHA-256 checksums](results/solarflare/repeated-20260905/manifest.json)
- [Compressed source text logs](results/solarflare/repeated-20260905/raw-logs.tar.gz)
- [Analysis and plotting script](scripts/plot_solarflare_repeated.py)

PNG and SVG versions of each plot are in the results directory. Source text
logs from these completed trials are archived together; loose copies are removed
after validation. Histogram CSVs are retained. Older exploratory logs are not
part of this cleanup or this repeated-trial analysis.

Recreate the tables and figures from the preserved dataset without NIC access:

```bash
PYTHONPATH=/tmp/erpc-plot-deps MPLCONFIGDIR=/tmp/erpc-mpl python3.11 scripts/plot_solarflare_repeated.py
```

The script writes `results/solarflare/repeated-20260905/report-section.md` alongside
the figures. This replaces the earlier single-run historical comparison for
current discussion; older reference figures remain in `results/solarflare/`.
'''
(OUT/'report-section.md').write_text(section)
print('Validated 25 latency + 18 concurrency + 3 DPDK histograms against 31 logs.')
print(section[:6500])
