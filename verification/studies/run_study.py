"""Independent bounded scientific comparison; all generated data are tests."""
import argparse,ctypes as C,hashlib,itertools,json,math,os,pathlib,subprocess,tempfile,zipfile,csv,platform
import numpy as np
import scipy.optimize
from scipy.stats import binomtest
from sklearn.linear_model import Ridge
p=argparse.ArgumentParser();p.add_argument('--cli',required=True);p.add_argument('--library',required=True);p.add_argument('--out',required=True);a=p.parse_args()
cli=pathlib.Path(a.cli).resolve();out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True);lib=C.CDLL(str(pathlib.Path(a.library).resolve()))
prereg=pathlib.Path(__file__).with_name('preregistration.json');registration=json.loads(prereg.read_text());(out/'preregistration_sha256.txt').write_text(hashlib.sha256(prereg.read_bytes()).hexdigest()+'\n')
class Matrix(C.Structure):_fields_=[('values',C.POINTER(C.c_double)),('rows',C.c_uint64),('columns',C.c_uint64),('stride',C.c_uint64)]
class Model(C.Structure):_fields_=[('features',C.c_uint64),('fitted_rows',C.c_uint64),('beta',C.c_double*17),('rmse',C.c_double)]
class SCM(C.Structure):_fields_=[('variables',C.c_uint32),('parents',C.c_uint32*4),('probabilities',(C.c_double*16)*4)]
ptr=C.POINTER(C.c_double)
lib.cp_fit_linear_checked.argtypes=[Matrix,ptr,C.POINTER(Model),C.c_void_p];lib.cp_fit_ridge.argtypes=[Matrix,ptr,C.c_double,C.POINTER(Model)];lib.cp_fit_huber.argtypes=[Matrix,ptr,C.c_double,C.c_uint,C.POINTER(Model),C.c_void_p]
lib.cp_scm_joint.argtypes=[C.POINTER(SCM),C.c_uint32,C.c_uint32,ptr];lib.cp_scm_effect.argtypes=[C.POINTER(SCM),C.c_uint,C.c_uint,ptr];lib.cp_conformal_radius.argtypes=[ptr,C.c_size_t,C.c_double,ptr]
lib.cp_source_identity.restype=C.c_char_p;lib.cp_build_identity.restype=C.c_char_p
checks=0;numeric=[];rng=np.random.default_rng(2026111004)
for trial in range(40):
 x=np.ascontiguousarray(rng.normal(size=(100,3)));y=np.ascontiguousarray(2+x@np.array([.4,-.8,1.2])+rng.normal(0,.03,100));view=Matrix(x.ctypes.data_as(ptr),100,3,3);m=Model()
 assert lib.cp_fit_linear_checked(view,y.ctypes.data_as(ptr),C.byref(m),None)==0
 reference=np.linalg.lstsq(np.column_stack((np.ones(100),x)),y,rcond=None)[0];err=float(np.max(np.abs(np.array(m.beta)[:4]-reference)));assert err<1e-10;checks+=1
 alpha=10**rng.uniform(-5,2);assert lib.cp_fit_ridge(view,y.ctypes.data_as(ptr),alpha,C.byref(m))==0
 ref=Ridge(alpha=alpha,solver='svd').fit(x,y);ridge_error=float(np.max(np.abs(np.array(m.beta)[:4]-np.r_[ref.intercept_,ref.coef_])));assert ridge_error<1e-9;checks+=1
 y[0]+=20
 assert lib.cp_fit_huber(view,y.ctypes.data_as(ptr),.2,1000,C.byref(m),None)==0
 ref=scipy.optimize.least_squares(lambda b:np.column_stack((np.ones(100),x))@b-y,reference,loss='huber',f_scale=.2,xtol=1e-13,ftol=1e-13,gtol=1e-13,max_nfev=1000)
 huber_error=float(np.max(np.abs(np.array(m.beta)[:4]-ref.x)));assert huber_error<1e-7;checks+=1
 numeric.append({'trial':trial,'qr_max_abs':err,'ridge_max_abs':ridge_error,'huber_max_abs':huber_error})
# Original-unit and response-scale stress, including subnormal coefficients.
for exponent in [-200,-100,0,100,200]:
 x=np.ascontiguousarray(np.linspace(-1,1,80).reshape(-1,1)*10.**exponent);y=np.ascontiguousarray(3+2*(x[:,0]/10.**exponent));m=Model();view=Matrix(x.ctypes.data_as(ptr),80,1,1)
 assert lib.cp_fit_linear_checked(view,y.ctypes.data_as(ptr),C.byref(m),None)==0
 assert abs(m.beta[0]-3)<1e-12 and abs(m.beta[1]/(2/10.**exponent)-1)<1e-12;checks+=1
# Independent truncated-factorization enumeration over randomly ordered DAGs.
scm_checks=0
for trial in range(100):
 n=4;m=SCM();m.variables=n;order=rng.permutation(n);prior=[]
 for v in order:
  mask=sum(1<<int(b) for b in prior if rng.random()<.5);m.parents[v]=mask;table={s:float(rng.uniform(.02,.98)) for s in range(16) if not s&~mask}
  for state in range(16):m.probabilities[v][state]=table[state&mask]
  prior.append(v)
 def independent(mask,values):
  probs=[]
  for state in range(16):
   probability=1.
   for v in range(n):
    bit=(state>>v)&1
    if mask>>v&1:probability*=float(bit==((values>>v)&1))
    else:
     prob=m.probabilities[v][state&m.parents[v]];probability*=prob if bit else 1-prob
   probs.append(probability)
  return np.array(probs)
 for mask in range(16):
  for values in range(16):
   if values&~mask:continue
   actual=(C.c_double*16)();assert lib.cp_scm_joint(C.byref(m),mask,values,actual)==0
   np.testing.assert_allclose(actual,independent(mask,values),atol=2e-15,rtol=2e-14);scm_checks+=1
 for treatment in range(4):
  for outcome in range(4):
   if treatment==outcome:continue
   effect=C.c_double();assert lib.cp_scm_effect(C.byref(m),treatment,outcome,C.byref(effect))==0
   reference=sum(independent(1<<treatment,1<<treatment)[s]-independent(1<<treatment,0)[s] for s in range(16) if s>>outcome&1)
   assert abs(effect.value-reference)<2e-14;scm_checks+=1
# Split-conformal radius against independent NumPy order statistics.
coverage=[];ood=[]
for trial in range(500):
 residuals=np.ascontiguousarray(abs(rng.normal(size=99)));radius=C.c_double(-1)
 assert lib.cp_conformal_radius(residuals.ctypes.data_as(ptr),99,.05,C.byref(radius))==0
 rank=math.ceil(100*(1-.05));assert radius.value==np.sort(residuals)[rank-1]
 coverage.append(float(np.mean(abs(rng.normal(size=100))<=radius.value)));ood.append(float(np.mean(abs(rng.normal(5,1,size=100))<=radius.value)));checks+=1
assert .93<np.mean(coverage)<.97
small=np.ascontiguousarray([1.,2.,3.]);radius=C.c_double(99);assert lib.cp_conformal_radius(small.ctypes.data_as(ptr),3,.05,C.byref(radius))==8 and radius.value==99;checks+=1
results=[];subprocesses=[]
def run(*args,ok=True):
 r=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True);subprocesses.append({'args':list(map(str,args)),'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 if ok and r.returncode:raise AssertionError(subprocesses[-1])
 return r
base={'schema_version':1,'experiment_kind':'caelum.discovery.v1','family':'finite_scm','policy':'eig','observations':'observations.csv','candidates':'candidates.csv','holdout':'holdout.csv','variables':[{'name':'x','unit':[0]*7,'lower':0,'upper':1,'role':'treatment','controllable':True},{'name':'y','unit':[0]*7,'lower':0,'upper':1,'role':'response','controllable':False}],'entropy':{'mode':'deterministic','seed':'0','stream':'1','purpose':'preregistered/selection'},'assumptions':{'interventions_authorized':True,'fully_observed':True,'iid_measurement_noise':True},'budget':{'max_interventions':10,'max_cost':10},'measurement_noise':.1,'cost_per_action':1,'hypotheses':[{'parents':[0,1],'probability_one':[[.5],[.1,.9]],'prior':1},{'parents':[0,1],'probability_one':[[.5],[.1,.2]],'prior':1}]}
with tempfile.TemporaryDirectory(prefix='caelum-preregistered-study-') as tmp,zipfile.ZipFile(out/'raw_active_runs.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
 root=pathlib.Path(tmp)
 for seed in range(1000,1030):
  folder=root/str(seed);folder.mkdir();gr=np.random.default_rng(seed)
  np.savetxt(folder/'observations.csv',np.column_stack((np.zeros(40,dtype=int),np.r_[np.ones(4,dtype=int),np.zeros(36,dtype=int)])),fmt='%d',delimiter=',')
  xs=np.arange(100)%2;ys=(gr.random(100)<np.where(xs,.9,.1)).astype(int);np.savetxt(folder/'candidates.csv',np.column_stack((xs,ys)),fmt='%d',delimiter=',')
  hx=gr.integers(0,2,500);hy=(gr.random(500)<np.where(hx,.9,.1)).astype(int);np.savetxt(folder/'holdout.csv',np.column_stack((hx,hy)),fmt='%d',delimiter=',')
  for policy in registration['active_study']['policies']:
   protocol=json.loads(json.dumps(base));protocol['policy']=policy;protocol['entropy']['seed']=f'{seed:x}';spec=folder/'protocol.json';spec.write_text(json.dumps(protocol,indent=2));bundle=folder/policy
   run('lab-run',spec,bundle);run('lab-run-replay',bundle);report=json.loads((bundle/'result.json').read_text());assert report['steps']==10 and report['cost']==10
   events=[json.loads(s) for s in (bundle/'events.jsonl').read_text().splitlines()];error=abs(report['finite_model_effect']-.8)
   results.append({'seed':seed,'policy':policy,'effect_error':error,'entropy':report['ensemble_entropy'],'holdout_brier':report['holdout_rmse']**2,'selected_x1':sum(int(xs[s['row']]) for s in events),'steps':report['steps'],'cost':report['cost']})
   for path in sorted(bundle.rglob('*')):
    if path.is_file() and path.name!='authority.lock':archive.write(path,f'{seed}/{policy}/{path.relative_to(bundle)}')
  print('completed seed',seed,flush=True)
 # Real hardware provenance, replayed without drawing another seed.
 for source in ['recorded','rdseed','rdrand']:
  protocol=json.loads(json.dumps(base));protocol['entropy']['mode']=source
  if source=='recorded':protocol['entropy']['seed']='1234'
  else:del protocol['entropy']['seed']
  spec=folder/f'{source}.json';spec.write_text(json.dumps(protocol));bundle=root/source
  run('lab-run',spec,bundle);run('lab-run-replay',bundle)
  for path in sorted(bundle.rglob('*')):
   if path.is_file() and path.name!='authority.lock':archive.write(path,f'entropy/{source}/{path.relative_to(bundle)}')
 # Full polynomial loop with an independent calibration split and sealed holdout.
 folder=root/'polynomial';folder.mkdir();prng=np.random.default_rng(777)
 for name,n in [('observations',80),('candidates',100),('calibration',99),('holdout',200)]:
  xx=prng.uniform(-2,2,n);yy=1+2*xx+xx*xx+prng.normal(0,.05,n);np.savetxt(folder/(name+'.csv'),np.column_stack((xx,yy)),fmt='%.17g',delimiter=',')
 specdata=json.loads(json.dumps(base));specdata.pop('hypotheses');specdata.update(family='polynomial',policy='disagreement',calibration='calibration.csv');specdata['variables'][0].update(role='input',lower=-2,upper=2);specdata['variables'][1].update(lower=-5,upper=15)
 spec=folder/'protocol.json';spec.write_text(json.dumps(specdata));run('lab-run',spec,folder/'run',3);run('lab-resume',folder/'run');run('lab-run-replay',folder/'run');polynomial=json.loads((folder/'run/result.json').read_text())
 for path in sorted((folder/'run').rglob('*')):
  if path.is_file() and path.name!='authority.lock':archive.write(path,f'polynomial/{path.relative_to(folder/"run")}')
comparisons=[]
for comparator in ['random','round_robin','cost_eig']:
 differences=[];wins=losses=ties=0
 for seed in range(1000,1030):
  eig=next(r for r in results if r['seed']==seed and r['policy']=='eig');other=next(r for r in results if r['seed']==seed and r['policy']==comparator);d=other['effect_error']-eig['effect_error'];differences.append(d)
  if d>1e-12:wins+=1
  elif d<-1e-12:losses+=1
  else:ties+=1
 comparisons.append({'eig_vs':comparator,'eig_wins':wins,'eig_losses':losses,'ties':ties,'mean_other_error_minus_eig_error':float(np.mean(differences)),'two_sided_sign_p':binomtest(wins,wins+losses).pvalue if wins+losses else 1.})
assert all(r['effect_error']==next(x['effect_error'] for x in results if x['seed']==r['seed'] and x['policy']=='eig') for r in results if r['policy']=='cost_eig')
summary={'status':'PASS','data':'known-truth generated mathematical test fixtures; no physical observations','preregistration_sha256':hashlib.sha256(prereg.read_bytes()).hexdigest(),'source_identity':lib.cp_source_identity().decode(),'build_identity':lib.cp_build_identity().decode(),'numeric_assertions':checks,'scm_enumeration_assertions':scm_checks,'numeric_trials':numeric,'conformal_mean_coverage':float(np.mean(coverage)),'conformal_ood_coverage_negative_control':float(np.mean(ood)),'active_runs':len(results),'policies':registration['active_study']['policies'],'comparisons':comparisons,'polynomial_run':polynomial,'limitations':['Bounded finite SCM class, imported interventional measurements, explicit declared assumptions.','Equal unit costs make cost_eig an exact control, not evidence of cost-aware advantage.','Conformal coverage requires exchangeability; the OOD failure is retained.','Sign tests are descriptive unadjusted comparisons, not universal superiority claims.'],'python':platform.python_version()}
(out/'study_results.json').write_text(json.dumps(summary,indent=2));(out/'subprocesses.json').write_text(json.dumps(subprocesses,indent=2))
with (out/'active_results.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(results[0]));w.writeheader();w.writerows(results)
print(json.dumps({k:v for k,v in summary.items() if k not in ['numeric_trials']},indent=2))
