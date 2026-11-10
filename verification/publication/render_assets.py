"""Render simple dark GitHub figures from the recorded scientific results."""
from pathlib import Path
import argparse,csv,json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();root=a.root.resolve();q=root/'evidence/qualification';assets=root/'docs/assets';assets.mkdir(parents=True,exist_ok=True)
style={'figure.facecolor':'#000000','axes.facecolor':'#000000','savefig.facecolor':'#000000','text.color':'#e8edf2','axes.labelcolor':'#c0c8d0','xtick.color':'#c0c8d0','ytick.color':'#c0c8d0','axes.edgecolor':'#46505b','font.family':'DejaVu Sans','font.size':11,'svg.fonttype':'none','svg.hashsalt':'caelum-0.4.0','axes.spines.top':False,'axes.spines.right':False}
plt.rcParams.update(style)
assets.joinpath('caelum-banner.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 235" role="img" aria-labelledby="title desc">
<title id="title">CAELUM Scientific Laboratory</title><desc id="desc">A native scientific laboratory. Version 0.4.0, C11, Linux x86-64.</desc>
<rect width="1200" height="235" fill="#000000"/>
<path d="M48 190H1152" stroke="#35404b"/>
<text x="48" y="104" fill="#f3f6f9" font-family="Arial, sans-serif" font-size="73" font-weight="600" letter-spacing="7">CAELUM</text>
<text x="51" y="150" fill="#b5c0cb" font-family="Arial, sans-serif" font-size="24">A native scientific laboratory.</text>
<text x="51" y="217" fill="#80d3da" font-family="monospace" font-size="15">HYPOTHESIS  /  EXPERIMENT  /  EVIDENCE</text>
<text x="1152" y="217" text-anchor="end" fill="#a8b4c0" font-family="monospace" font-size="15">0.4.0  ·  C11  ·  LINUX x86-64</text>
</svg>\n''')
def base(title):
 fig,ax=plt.subplots(figsize=(10.4,4.25));fig.subplots_adjust(left=.11,right=.96,top=.81,bottom=.24);fig.text(.055,.92,title,fontsize=17,color='#f0f4f8',weight='semibold');ax.grid(axis='y',alpha=.22,color='#67717c',linewidth=.6);ax.set_axisbelow(True);return fig,ax
def finish(fig,name,note):
 fig.text(.055,.055,note,fontsize=9,color='#aeb9c4');fig.savefig(assets/(name+'.svg'),metadata={'Date':None});fig.savefig(assets/(name+'.png'),dpi=160);svg_path=assets/(name+'.svg');svg=svg_path.read_text();at=svg.index('>',svg.index('<svg'))+1;svg_path.write_text(svg[:at]+'\n<rect width="100%" height="100%" fill="#000000"/>'+svg[at:]);plt.close(fig)
with (q/'active_study/active_results.csv').open() as f:rows=list(csv.DictReader(f))
policies=['random','round_robin','eig','cost_eig'];data=[[float(r['effect_error']) for r in rows if r['policy']==key] for key in policies]
fig,ax=base('Equal-budget intervention selection');box=ax.boxplot(data,tick_labels=['Random','Fixed order','EIG','EIG / cost'],patch_artist=True,widths=.44,medianprops={'color':'#ffffff','linewidth':1.3},boxprops={'edgecolor':'#a3adb8'},whiskerprops={'color':'#8c98a4'},capprops={'color':'#8c98a4'},flierprops={'marker':'o','markersize':4,'markerfacecolor':'none','markeredgecolor':'#c1cbd6'})
for patch,color in zip(box['boxes'],['#2b343d','#2b343d','#236972','#236972']):patch.set_facecolor(color)
ax.set_ylabel('Absolute effect error');ax.set_ylim(-.025,.65);ax.set_yticks([0,.2,.4,.6]);finish(fig,'intervention-error','30 seeds per policy · 10 actions / cost 10 · mathematical fixtures · all trials retained')
emp=json.loads((q/'empirical/empirical_results.json').read_text());held=np.genfromtxt(q/'empirical/heldout_predictions.csv',delimiter=',',names=True);index=emp['selected_by_training_bic'];observed=held['ir_3'];pred=held[['pred_template0','pred_template1','pred_linear'][index]]
fig,ax=base('Prediction on published measurements');lo=min(float(observed.min()),float(pred.min()));hi=max(float(observed.max()),float(pred.max()));ax.plot([lo,hi],[lo,hi],color='#707d8b',ls='--',lw=1);ax.scatter(observed,pred,s=16,c='#80d3da',alpha=.78,edgecolors='none');ax.set_xlabel('Observed ir_3 (recorded sensor units)');ax.set_ylabel('Native prediction');ax.grid(alpha=.16,color='#67717c',linewidth=.6)
finish(fig,'empirical-fit','200 reserved measurements · supplied template · Gamella, Peters & Bühlmann (2025) · CC BY 4.0')
study=json.loads((q/'active_study/study_results.json').read_text());coverage=[study['conformal_mean_coverage'],study['conformal_ood_coverage_negative_control'],emp['selected_holdout_interval_coverage']]
fig,ax=base('Calibration and its failure boundary');bars=ax.bar(['Exchangeable fixtures','Shifted negative control','Published holdout'],coverage,width=.43,color=['#236972','#663e45','#236972']);ax.axhline(.95,color='#c4ced8',ls='--',lw=1);ax.set_ylim(0,1.12);ax.set_yticks([0,.25,.5,.75,1.]);ax.set_ylabel('Measured interval coverage')
for bar,v in zip(bars,coverage):ax.text(bar.get_x()+bar.get_width()/2,v+.035,f'{100*v:.2f}%',ha='center',color='#edf3f8',fontsize=11)
finish(fig,'calibration','Dashed line: nominal 95% · exchangeability is required · shifted failure retained explicitly')
print('Rendered banner and three black-background graphs from checked-in results.')
