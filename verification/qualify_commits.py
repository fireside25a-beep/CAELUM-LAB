"""Actual subprocess interruption; no production fault switches or mock IO."""
import argparse,hashlib,json,os,pathlib,subprocess,tempfile,ctypes as C,threading,resource,signal
p=argparse.ArgumentParser();p.add_argument('--cli',required=True);p.add_argument('--library',required=True);p.add_argument('--out',required=True);a=p.parse_args()
cli=pathlib.Path(a.cli).resolve();root=pathlib.Path(a.out).resolve();root.mkdir(parents=True,exist_ok=True)
source=pathlib.Path(__file__).with_name('commit_faults.c');hook=root/'commit_faults.so'
subprocess.run(['cc','-std=c11','-shared','-fPIC',str(source),'-ldl','-o',str(hook)],check=True)
logs=[]
def call(args,env=None,ok=True,size_limit=None):
 def limit():
  signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(size_limit,size_limit))
 r=subprocess.run([str(cli),*map(str,args)],env=env,capture_output=True,text=True,preexec_fn=limit if size_limit is not None else None)
 logs.append({'args':list(map(str,args)),'code':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 with (root/'subprocess_trace.jsonl').open('a') as trace:trace.write(json.dumps(logs[-1])+'\n')
 if ok and r.returncode:raise AssertionError(logs[-1])
 return r
with tempfile.TemporaryDirectory(prefix='caelum-faults-') as tmp:
 d=pathlib.Path(tmp)
 for name,offset in [('old',1),('new',7)]:
  (d/(name+'.csv')).write_text(''.join(f'{i},{offset+2*i+i*i}\n' for i in range(40)))
  call(['lab-discover',d/(name+'.csv'),2,'1234',d/(name+'.cpr')])
 def export(which,env=None,ok=True):return call(['lab-export',d/(which+'.csv'),d/(which+'.cpr'),d/'model.cpm',d/'basis.txt'],env,ok)
 def clear_outputs():
  for f in d.iterdir():
   if f.name.startswith(('model.cpm','basis.txt')):f.unlink()
 def validate(old,new):
  call(['inspect',d/'model.cpm'])
  model=(d/'model.cpm').read_bytes();basis=(d/'basis.txt').read_bytes()
  assert (model,basis) in (old,new),'mixed pair after recovery'
  assert f'model_sha256={hashlib.sha256(model).hexdigest()}'.encode() in basis
  call(['lab-predict-export',d/'model.cpm',d/'basis.txt',3])
  return 'old' if (model,basis)==old else 'new'
 export('old');old=((d/'model.cpm').read_bytes(),(d/'basis.txt').read_bytes())
 export('new');new=((d/'model.cpm').read_bytes(),(d/'basis.txt').read_bytes())
 trials=[]
 for mode,maximum in [('CP_KILL_BEFORE_RENAME',10),('CP_KILL_AFTER_RENAME',10),('CP_FAIL_FSYNC',40)]:
  for n in range(1,maximum+1):
   clear_outputs();export('old');env=dict(os.environ,LD_PRELOAD=str(hook),**{mode:str(n)})
   print(mode,n,flush=True);r=export('new',env,False);outcome=validate(old,new);trials.append({'mode':mode,'n':n,'code':r.returncode,'recovered':outcome})
 for second_mode,maximum in [('CP_KILL_AFTER_RENAME',4),('CP_KILL_AFTER_UNLINK',6)]:
  for n in range(1,maximum+1):
   clear_outputs();export('old');export('new',dict(os.environ,LD_PRELOAD=str(hook),CP_KILL_AFTER_RENAME='3'),False)
   r=call(['inspect',d/'model.cpm'],dict(os.environ,LD_PRELOAD=str(hook),**{second_mode:str(n)}),False)
   outcome=validate(old,new);trials.append({'mode':'second_crash/'+second_mode,'n':n,'code':r.returncode,'recovered':outcome})
 clear_outputs();(d/'basis.txt').symlink_to(d/'model.cpm');r=export('new',ok=False);assert r.returncode and not(d/'model.cpm').exists();trials.append({'mode':'dangling_alias','code':r.returncode})
 # The first-party text adapter must use the same atomic sink.
 clear_outputs();export('old');adapter=cli.parent/'cp_linear_exchange.so'
 if not adapter.exists():adapter=cli.parent.parent/'lib/cp_linear_exchange.so'
 assert adapter.exists(),adapter
 text=d/'external.txt';call(['export',adapter,d/'model.cpm',text]);old_text=text.read_bytes()
 export('new');call(['export',adapter,d/'model.cpm',text]);new_text=text.read_bytes()
 for n in range(1,8):
  text.write_bytes(old_text)
  env=dict(os.environ,LD_PRELOAD=str(hook),CP_FAIL_FSYNC=str(n))
  r=call(['export',adapter,d/'model.cpm',text],env,False)
  assert text.read_bytes() in (old_text,new_text),'Partial text-adapter commit'
  trials.append({'mode':'text_adapter_fsync','n':n,'code':r.returncode})
 text.write_bytes(old_text)
 r=call(['export',adapter,d/'model.cpm',text],ok=False,size_limit=32)
 assert r.returncode and text.read_bytes()==old_text,'Size-limited text export changed the existing destination'
 trials.append({'mode':'text_adapter_RLIMIT_FSIZE','code':r.returncode,'existing_destination_preserved':True})
 model_before=(d/'model.cpm').read_bytes()
 r=call(['fit',d/'old.csv',d/'model.cpm'],ok=False,size_limit=32)
 assert r.returncode and (d/'model.cpm').read_bytes()==model_before,'Size-limited capsule export changed the existing destination'
 trials.append({'mode':'capsule_RLIMIT_FSIZE','code':r.returncode,'existing_destination_preserved':True})
 # Snapshot consistency while an independent writer atomically switches files.
 class Dataset(C.Structure):_fields_=[('x',C.POINTER(C.c_double)),('y',C.POINTER(C.c_double)),('rows',C.c_uint64),('features',C.c_uint64)]
 lib=C.CDLL(str(pathlib.Path(a.library).resolve()));lib.cp_dataset_read_snapshot.argtypes=[C.c_char_p,C.POINTER(Dataset),C.POINTER(C.c_uint8)];lib.cp_dataset_release.argtypes=[C.POINTER(Dataset)]
 sources=[b'0,1\n1,3\n2,5\n',b'0,7\n1,9\n2,11\n'];mapping={hashlib.sha256(x).digest():i for i,x in enumerate(sources)}
 target=d/'snapshot.csv';target.write_bytes(sources[0]);stop=threading.Event();errors=[]
 def writer():
  try:
   i=0
   while not stop.is_set():
    stage=d/'switch.tmp';stage.write_bytes(sources[i%2]);os.replace(stage,target);i+=1
  except Exception as e:errors.append(str(e))
 thread=threading.Thread(target=writer);thread.start()
 try:
  for i in range(1000):
   ds=Dataset();h=(C.c_uint8*32)();assert lib.cp_dataset_read_snapshot(os.fsencode(target),C.byref(ds),h)==0
   index=mapping[bytes(h)];assert ds.rows==3 and ds.features==1
   assert [ds.y[j] for j in range(3)]==([1,3,5] if index==0 else [7,9,11]);lib.cp_dataset_release(C.byref(ds))
 finally:stop.set();thread.join()
 assert not errors
 result={'status':'PASS','pair_trials':trials,'snapshot_replacements_checked':1000,'limits':'Local-process crash and fsync-failure testing; not a physical power-loss test. Pre-journal crashes may leave harmless staged temporary files; no automatic deletion of unknown user files.'}
 (root/'commit_qualification.json').write_text(json.dumps(result,indent=2));(root/'subprocess_logs.json').write_text(json.dumps(logs,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='pair_trials'}));print('pair trials',len(trials))
