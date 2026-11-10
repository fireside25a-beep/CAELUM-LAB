import argparse,json,pathlib,subprocess,tempfile,hashlib,os
p=argparse.ArgumentParser();p.add_argument('--cli',required=True);p.add_argument('--out',required=True);a=p.parse_args();cli=pathlib.Path(a.cli).resolve();out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
checks=[];logs=[]
def run(args,ok=True):
 r=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True);logs.append({'args':list(map(str,args)),'code':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 assert (r.returncode==0)==ok,logs[-1]
 return r
base={'schema_version':1,'experiment_kind':'caelum.discovery.v1','family':'finite_scm','policy':'eig','observations':'observations.csv','candidates':'candidates.csv','holdout':'holdout.csv','variables':[{'name':'x','unit':[0]*7,'lower':0,'upper':1,'role':'treatment','controllable':True},{'name':'y','unit':[0]*7,'lower':0,'upper':1,'role':'response','controllable':False}],'entropy':{'mode':'deterministic','seed':'1234','stream':'1','purpose':'edges/selection'},'assumptions':{'interventions_authorized':True,'fully_observed':True,'iid_measurement_noise':True},'budget':{'max_interventions':10,'max_cost':10},'measurement_noise':.1,'cost_per_action':1,'hypotheses':[{'parents':[0,1],'probability_one':[[.5],[.1,.9]],'prior':1},{'parents':[0,1],'probability_one':[[.5],[.1,.2]],'prior':1}]}
with tempfile.TemporaryDirectory(prefix='caelum-engine-edges-') as tmp:
 d=pathlib.Path(tmp)
 (d/'observations.csv').write_text(''.join(f'{i%2},{int(i%7==0)}\n' for i in range(40)))
 (d/'candidates.csv').write_text(''.join(f'{i%2},{int(i%3!=0)}\n' for i in range(60)))
 (d/'holdout.csv').write_text(''.join(f'{i%2},{int(i%5==0)}\n' for i in range(100)))
 spec=d/'protocol.json'
 def write(config):spec.write_text(json.dumps(config))
 write(base);run(['lab-run',spec,d/'base']);events=[json.loads(x) for x in (d/'base/events.jsonl').read_text().splitlines()]
 (d/'holdout.csv').write_text(''.join(f'{i%2},{int(i%5!=0)}\n' for i in range(100)));run(['lab-run',spec,d/'holdout_changed']);assert (d/'base/events.jsonl').read_bytes()==(d/'holdout_changed/events.jsonl').read_bytes();checks.append('Holdout outcome mutation cannot alter any selection/update event')
 (d/'candidates.csv').write_text(''.join(f'{i%2},{int(i%3==0)}\n' for i in range(60)));run(['lab-run',spec,d/'outcomes_changed',1]);other=json.loads((d/'outcomes_changed/events.jsonl').read_text());assert other['row']==events[0]['row'] and other['prediction']==events[0]['prediction'];checks.append('First action is independent of candidate outcomes')
 for name,change in [('latent_confounding',lambda c:c['assumptions'].update(fully_observed=False)),('no_authority',lambda c:c['assumptions'].update(interventions_authorized=False)),('uncontrolled',lambda c:c['variables'][0].update(controllable=False)),('negative_seed',lambda c:c['entropy'].update(seed='-1')),('unknown_field',lambda c:c.update(unimplemented=True))]:
  config=json.loads(json.dumps(base));change(config);write(config);run(['lab-run',spec,d/name],False);checks.append('Refused '+name)
 # Very small nonzero posterior mass is numerical underflow, not logical falsification.
 config=json.loads(json.dumps(base));config['budget']['max_interventions']=0
 config['hypotheses'][1]['probability_one'][1]=[.999999999999,.2]
 (d/'observations.csv').write_text('0,0\n'*40);write(config)
 run(['lab-run',spec,d/'underflow']);report=json.loads((d/'underflow/result.json').read_text());assert report['finite_model_support'][0]<-.79 and report['rejected_models']==0;checks.append('Posterior underflow cannot erase an unfalsified model from support bounds')
 run(['lab-discover',d/'observations.csv',1,'-1',d/'negative.cpr'],False);checks.append('CLI rejects signed hexadecimal seeds')
 # Fixed deterministic mechanisms can be logically falsified, without inventing a positive conclusion.
 config=json.loads(json.dumps(base));
 for h in config['hypotheses']:h['probability_one'][1]=[0.,0.]
 (d/'observations.csv').write_text(''.join(f'{i%2},0\n' for i in range(40)));(d/'candidates.csv').write_text(''.join(f'{i%2},1\n' for i in range(60)));write(config)
 run(['lab-run',spec,d/'falsified']);report=json.loads((d/'falsified/result.json').read_text());assert report['phase']==5 and report['steps']==1 and report['rejected_models']==2;run(['lab-run-replay',d/'falsified']);checks.append('All deterministic hypotheses falsified: explicit COUNTEREXAMPLE, exact replay')
 # The captured protocol is immutable evidence, even though replay uses saved configuration.
 (d/'falsified/protocol.json').write_text('{}');run(['lab-run-replay',d/'falsified'],False);checks.append('Tampered frozen protocol refused')
 # Three-input polynomial runs must retain supported degrees when the cubic
 # dictionary exceeds the documented sixteen-term capacity.
 import math
 config=json.loads(json.dumps(base));config.pop('hypotheses');config.update(family='polynomial',policy='disagreement',calibration='calibration.csv');config['budget']['max_interventions']=1
 config['variables']=[{'name':f'x{k}','unit':[0]*7,'lower':-1,'upper':1,'role':'input','controllable':True} for k in range(3)]+[{'name':'y','unit':[0]*7,'lower':-10,'upper':10,'role':'response','controllable':False}]
 for name,n,offset in [('observations',80,0),('candidates',80,100),('calibration',99,200),('holdout',100,400)]:
  lines=[]
  for i in range(offset,offset+n):
   x0=math.sin(i*.7);x1=math.cos(i*.37);x2=math.sin(i*.19+.5);y=1+x0+2*x1*x1+.01*math.sin(i)
   lines.append(','.join(format(v,'.17g') for v in [x0,x1,x2,y]))
  (d/(name+'.csv')).write_text('\n'.join(lines)+'\n')
 write(config);run(['lab-run',spec,d/'three_inputs']);run(['lab-run-replay',d/'three_inputs']);checks.append('Three-input polynomial loop skips unsupported cubic dictionary and runs supported models')
 (d/'observations.csv').write_text('0,0,0,0\n'*20);write(config);run(['lab-run',spec,d/'insufficient_fit'],False);checks.append('Insufficient polynomial fit is an error, not a scientific counterexample')
 # Duplicate escaped aliases are rejected before semantic parsing.
 spec.write_text('{"schema_version":1,"\\u0073chema_version":1}');run(['lab-run',spec,d/'duplicate'],False);checks.append('Duplicate decoded keys refused')
(out/'edge_results.json').write_text(json.dumps({'status':'PASS','checks':checks,'subprocesses':logs},indent=2));print('PASS',len(checks),'authority/leakage/falsification/protocol checks')
