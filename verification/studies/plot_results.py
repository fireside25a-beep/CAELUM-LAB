"""Source-backed scientific figure from recorded qualification results."""
import argparse,csv,json,pathlib
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('release');a=p.parse_args();root=pathlib.Path(a.release);q=root/'evidence/qualification';study=json.loads((q/'active_study/study_results.json').read_text());empirical=json.loads((q/'empirical/empirical_results.json').read_text())
with (q/'active_study/active_results.csv').open() as f:rows=list(csv.DictReader(f))
fig,axes=plt.subplots(1,3,figsize=(15.5,4.7));fig.patch.set_facecolor('#f7f9fc');colors=['#b4534b','#8d789c','#2171a7','#248b83']
policies=['random','round_robin','eig','cost_eig'];values=[[float(r['effect_error']) for r in rows if r['policy']==name] for name in policies]
bp=axes[0].boxplot(values,tick_labels=['Random','Fixed order','EIG','EIG / cost'],patch_artist=True,showfliers=True)
for patch,color in zip(bp['boxes'],colors):patch.set_facecolor(color);patch.set_alpha(.7)
axes[0].set_title('Finite-class intervention test',loc='left',fontweight='bold');axes[0].set_ylabel('Absolute effect error (30 seeds)');axes[0].tick_params(axis='x',rotation=18);axes[0].grid(axis='y',alpha=.2);axes[0].text(.01,-.29,'10 actions / cost 10 for every run; all costs = 1.\nKnown-truth mathematical fixtures; losing trials retained.',transform=axes[0].transAxes,fontsize=9,color='#43556b')
held=np.genfromtxt(q/'empirical/heldout_predictions.csv',delimiter=',',names=True);selected=empirical['selected_by_training_bic'];pred=held[['pred_template0','pred_template1','pred_linear'][selected]];observed=held['ir_3'];lo=min(observed.min(),pred.min());hi=max(observed.max(),pred.max())
axes[1].scatter(observed,pred,s=17,color='#2171a7',alpha=.72,linewidths=0);axes[1].plot([lo,hi],[lo,hi],'--',color='#7b8798',lw=1);axes[1].set_title('Published physical measurements',loc='left',fontweight='bold');axes[1].set_xlabel('Observed ir_3 (recorded sensor units)');axes[1].set_ylabel('Native model prediction');axes[1].grid(alpha=.15);axes[1].text(.01,-.29,f"200 reserved rows; RMSE = {empirical['models'][selected]['holdout_rmse']:.2f}.\nSupplied templates; chamber operated by dataset authors.",transform=axes[1].transAxes,fontsize=9,color='#43556b')
coverage=[study['conformal_mean_coverage'],study['conformal_ood_coverage_negative_control'],empirical['selected_holdout_interval_coverage']];bars=axes[2].bar(['Exchangeable\nfixtures','Shifted\nnegative control','Published\nholdout'],coverage,color=['#248b83','#b4534b','#2171a7']);axes[2].axhline(.95,color='#4c5c70',linestyle='--',lw=1);axes[2].set_ylim(0,1.12);axes[2].set_ylabel('Measured interval coverage');axes[2].set_title('Calibration and its failure boundary',loc='left',fontweight='bold');axes[2].grid(axis='y',alpha=.15)
for bar,value in zip(bars,coverage):axes[2].text(bar.get_x()+bar.get_width()/2,value+.025,f'{100*value:.2f}%',ha='center',fontsize=10)
axes[2].text(.01,-.29,'Dashed line: nominal 95% marginal coverage.\nExchangeability is required; shift failure is explicit.',transform=axes[2].transAxes,fontsize=9,color='#43556b')
for ax in axes:
 ax.spines[['top','right']].set_visible(False);ax.set_facecolor('white')
fig.subplots_adjust(left=.06,right=.985,top=.88,bottom=.28,wspace=.35);dest=root/'docs/figures';dest.mkdir(parents=True,exist_ok=True);fig.savefig(dest/'qualification_summary.png',dpi=180);fig.savefig(dest/'qualification_summary.svg');plt.close(fig)
