#!/usr/bin/env python3
"""Publication figure from the actual packaged numerical case reports."""
from pathlib import Path
import re
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(__file__).resolve().parent.parent
rows=[]
for name in ['uniform','irregular']:
 text=(root/'cases'/('dynamics_'+name+'.txt')).read_text()
 match=re.search(r'forward_rmse=(\S+) reverse_observed_rmse=(\S+) solver_cycle_rmse=(\S+) solver_cycle_max_abs=(\S+)',text)
 if not match:raise ValueError('absent measured audit')
 rows.append((name,*map(float,match.groups())))
with (root/'cases/dynamics_metrics.csv').open('w') as f:
 w=csv.writer(f);w.writerow(['trajectory','forward_rmse','reverse_observed_rmse','cycle_rmse','cycle_max_abs']);w.writerows(rows)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,ax=plt.subplots(figsize=(8.2,4.8))
fig.subplots_adjust(left=.13,right=.98,bottom=.20,top=.76)
x=np.arange(2);labels=['Forward prediction','Backward from observation','Numerical cycle']
colors=['#246a9e','#d17b24','#487a63']
for j,(label,color) in enumerate(zip(labels,colors)):
 ax.bar(x+(j-1)*.23,[r[j+1] for r in rows],width=.21,label=label,color=color)
ax.set_yscale('log');ax.set_ylim(1e-18,1e-3);ax.set_xticks(x,['Uniform grid','Irregular grid'])
ax.set_ylabel('RMSE (dimensionless fixture states)')
fig.suptitle('Model error and numerical reversal measure different things',x=.13,y=.97,ha='left',fontsize=12,fontweight='bold')
ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True);ax.legend(loc='upper center',bbox_to_anchor=(.5,1.22),ncol=3,frameon=False,fontsize=8)
fig.text(.98,.04,'CAELUM 0.3.1 · native Linux x86-64 · known-truth mathematical fixtures',ha='right',fontsize=8,color='#555555')
fig.savefig(root/'docs/dynamics_validation.png',dpi=180)
plt.close(fig)
print('PASS figure derived from packaged case reports')
