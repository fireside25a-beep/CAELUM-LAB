import argparse,pathlib,shutil,subprocess,tempfile,json
p=argparse.ArgumentParser();p.add_argument('--cmake',required=True);p.add_argument('--out',required=True);a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
mutations=[('generator_multiplier','src/random_v2.c','0xd2511f53','0xd2511f55','lab_engine_contracts'),('midpoint_tolerance','src/dynamics.c','fabs(error[k])/scale','fabs(error[k])/fmax(scale,1e20)','release_regressions'),('huber_unweighted','src/numeric.c','if(step&&error>0)','if(0&&step&&error>0)','lab_engine_contracts')]
results=[]
for name,file,before,after,test in mutations:
 with tempfile.TemporaryDirectory(prefix='caelum-mutation-') as tmp:
  folder=pathlib.Path(tmp)/'source';folder.mkdir()
  for part in ['src','include','adapters','tests','examples']:shutil.copytree(root/part,folder/part)
  (folder/'verification').mkdir();shutil.copyfile(root/'verification/parser_fuzz.c',folder/'verification/parser_fuzz.c');shutil.copyfile(root/'CMakeLists.txt',folder/'CMakeLists.txt')
  path=folder/file;s=path.read_text();assert before in s;path.write_text(s.replace(before,after,1));build=pathlib.Path(tmp)/'build'
  logs=[]
  for command in [[a.cmake,'-S',str(folder),'-B',str(build),'-DCMAKE_BUILD_TYPE=Debug'],[a.cmake,'--build',str(build),'-j4']]:
   r=subprocess.run(command,capture_output=True,text=True);logs.append(r.stdout+r.stderr);assert r.returncode==0
  r=subprocess.run(['ctest','--test-dir',str(build),'-R','^'+test+'$','--output-on-failure'],capture_output=True,text=True);assert r.returncode!=0,'Mutation escaped its required regression test'
  results.append({'mutation':name,'expected':'test failure','observed_exit':r.returncode,'test':test,'output':r.stdout+r.stderr});(out/(name+'.log')).write_text('\n'.join(logs)+r.stdout+r.stderr)
(out/'mutation_results.json').write_text(json.dumps({'status':'PASS','killed_mutants':len(results),'scope':'Three deliberate negative controls, not a claimed exhaustive mutation score. Current candidate sources were never edited.','results':results},indent=2));print('PASS all',len(results),'deliberate source mutants detected')
