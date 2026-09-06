"""Generate advisor figures from recorded logs; requires Matplotlib."""
from pathlib import Path
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root = Path(__file__).resolve().parent
repo = root.parent.parent
ping = (repo/'dpdk_pingpong/pingpong_64_20260905_160808_103495.txt').read_text()
last = [l.split() for l in ping.splitlines() if l.startswith('64 ')][-1]
rate = (repo/'server_rate_preflight_20260905_162812_109121.txt').read_text()
summary = next(l for l in rate.splitlines() if l.startswith('PREFLIGHT_SUMMARY'))
r = dict(re.findall(r'([\w.]+)=([\d.]+)', summary))
fig, ax = plt.subplots(figsize=(7.5,4.5), layout='constrained')
x = [0,1,2]
a = [float(last[i]) for i in [1,2,3]]
b = [float(r[k]) for k in ['median_us','p99_us','p99.9_us']]
for shift, data, label in [(-.18,a,'DPDK ping-pong'),(.18,b,'eRPC server_rate, window 1')]:
    bars = ax.bar([i+shift for i in x],data,width=.36,label=label)
    ax.bar_label(bars,fmt='%.1f',padding=3)
ax.set_xticks(x,['Median','p99','p99.9'])
ax.set_ylabel('Full round-trip latency (µs)')
ax.set_ylim(0,20)
ax.set_title('Phase 0: observed local baselines\n64-byte request / 8-byte reply · one run each')
ax.legend(loc='upper left')
ax.grid(axis='y',alpha=.2)
fig.supxlabel('Queue/process layouts and timing differ; the gap is not isolated protocol cost.',fontsize=9)
fig.savefig(root/'phase0-baseline.png',dpi=180)
fig.savefig(root/'phase0-baseline.svg')
plt.close(fig)
fig,axes=plt.subplots(1,2,figsize=(10,4.5),layout='constrained')
for name,label,style in [('solarflare_latency_20260905_154418_94756.txt','Before affinity fix','o--'),
                         ('solarflare_latency_20260905_155630_99294.txt','After affinity fix','s-')]:
    lines = [l.split() for l in (repo/name).read_text().splitlines() if l.endswith('30 seconds]')]
    assert [int(l[0]) for l in lines]==[128,256,512,1024]
    for ax,col in zip(axes,[1,3]):
        ax.plot([int(l[0]) for l in lines],[float(l[col]) for l in lines],style,label=label)
for ax,label in zip(axes,['Median RTT (µs)','p99 RTT (µs)']):
    ax.set_xscale('log',base=2)
    ax.set_xticks([128,256,512,1024],['128','256','512','1024'])
    ax.set_xlabel('Request payload (bytes)'); ax.set_ylabel(label)
    ax.set_ylim(bottom=0); ax.grid(alpha=.2); ax.legend()
fig.suptitle('Phase 0: request-size sensitivity\n8-byte responses · 5 s warmup + 30 s measurement')
fig.supxlabel('One run per point; the post-fix 64-byte latency-app measurement has not been collected.',fontsize=9)
fig.savefig(root/'phase0-size.png',dpi=180)
fig.savefig(root/'phase0-size.svg')
print('Generated baseline and size figures from recorded logs.')
