"""Published physical measurements; supplied templates, not discovered physics."""
import argparse,ctypes as C,csv,hashlib,io,json,math,pathlib,zipfile
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--data',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True);source=pathlib.Path(a.data);z=source/'lt_malus_v1.zip';spec=pathlib.Path(__file__).with_name('empirical_preregistration.json');registration=json.loads(spec.read_text());assert hashlib.md5(z.read_bytes()).hexdigest()==registration['upstream_md5']
lib=C.CDLL(str(pathlib.Path(a.library).resolve()));ptr=C.POINTER(C.c_double)
class Unit(C.Structure):_fields_=[('exponent',C.c_int8*7)]
class Variable(C.Structure):_fields_=[('name',C.c_char*32),('unit',Unit),('lower',C.c_double),('upper',C.c_double),('role',C.c_uint32),('controllable',C.c_uint32)]
class Node(C.Structure):_fields_=[('op',C.c_uint32),('left',C.c_uint32),('right',C.c_uint32),('variable',C.c_uint32),('power',C.c_int32),('value',C.c_double),('unit',Unit)]
class Expression(C.Structure):_fields_=[('inputs',C.c_uint32),('count',C.c_uint32),('variables',Variable*3),('nodes',Node*256)]
class Matrix(C.Structure):_fields_=[('values',ptr),('rows',C.c_uint64),('columns',C.c_uint64),('stride',C.c_uint64)]
class Model(C.Structure):_fields_=[('features',C.c_uint64),('fitted_rows',C.c_uint64),('beta',C.c_double*17),('rmse',C.c_double)]
lib.cp_expression_evaluate.argtypes=[C.POINTER(Expression),ptr,ptr,ptr];lib.cp_fit_linear_checked.argtypes=[Matrix,ptr,C.POINTER(Model),C.c_void_p];lib.cp_predict_linear.argtypes=[C.POINTER(Model),ptr,ptr];lib.cp_conformal_radius.argtypes=[ptr,C.c_size_t,C.c_double,ptr];lib.cp_source_identity.restype=C.c_char_p;lib.cp_build_identity.restype=C.c_char_p
with zipfile.ZipFile(z) as archive:raw=archive.read('lt_malus_v1/white_255.csv');rows=list(csv.DictReader(io.StringIO(raw.decode())))
assert len(rows)==1000
x=np.ascontiguousarray([[math.radians(float(row['pol_1'])),math.radians(float(row['pol_2']))] for row in rows]);y=np.ascontiguousarray([float(row['ir_3']) for row in rows])
expressions=[]
for kind in range(2):
 e=Expression();e.inputs=2;e.count=5 if kind==0 else 8
 for j in range(2):e.variables[j]=Variable(f'theta_{j+1}_radians'.encode(),Unit(),-math.pi,math.pi,1,1)
 e.nodes[0]=Node(op=2,variable=0);e.nodes[1]=Node(op=2,variable=1);e.nodes[2]=Node(op=4,left=0,right=1);e.nodes[3]=Node(op=11,left=2);e.nodes[4]=Node(op=7,left=3,power=2)
 e.nodes[5]=Node(op=11,left=0);e.nodes[6]=Node(op=7,left=5,power=2);e.nodes[7]=Node(op=5,left=4,right=6);expressions.append(e)
features=[]
for e in expressions:
 phi=np.empty((1000,1),dtype=float)
 for i in range(1000):
  result=C.c_double();assert lib.cp_expression_evaluate(C.byref(e),x[i].ctypes.data_as(ptr),C.byref(result),None)==0;phi[i,0]=result.value
 features.append(np.ascontiguousarray(phi))
np.testing.assert_allclose(features[0][:,0],np.cos(x[:,0]-x[:,1])**2,atol=2e-15);np.testing.assert_allclose(features[1][:,0],np.cos(x[:,0])**2*np.cos(x[:,0]-x[:,1])**2,atol=3e-15)
features.append(x);results=[];predictions=[]
for name,phi in zip(registration['rival_models'],features):
 m=Model();view=Matrix(phi.ctypes.data_as(ptr),600,phi.shape[1],phi.shape[1]);assert lib.cp_fit_linear_checked(view,y.ctypes.data_as(ptr),C.byref(m),None)==0
 predicted=np.empty(1000)
 for i in range(1000):
  value=C.c_double();assert lib.cp_predict_linear(C.byref(m),phi[i].ctypes.data_as(ptr),C.byref(value))==0;predicted[i]=value.value
 design=np.column_stack((np.ones(1000),phi));reference=design@np.linalg.lstsq(design[:600],y[:600],rcond=None)[0];discrepancy=float(np.max(abs(reference-predicted)));assert discrepancy<1e-8
 mse=float(np.mean((predicted[:600]-y[:600])**2));bic=600*math.log(max(mse,np.finfo(float).tiny))+design.shape[1]*math.log(600);rmse=float(np.sqrt(np.mean((predicted[800:]-y[800:])**2)))
 results.append({'template':name,'coefficients':list(m.beta)[:phi.shape[1]+1],'training_bic':bic,'holdout_rmse':rmse,'native_numpy_max_abs_prediction_error':discrepancy});predictions.append(predicted)
selected=int(np.argmin([r['training_bic'] for r in results]));residuals=np.ascontiguousarray(abs(predictions[selected][600:800]-y[600:800]));radius=C.c_double();assert lib.cp_conformal_radius(residuals.ctypes.data_as(ptr),200,.05,C.byref(radius))==0;coverage=float(np.mean(abs(predictions[selected][800:]-y[800:])<=radius.value))
summary={'status':'PASS','origin':'Previously published physical chamber measurements collected by Gamella et al.; no chamber was operated here.','dataset':'lt_malus_v1/white_255.csv','rows':1000,'input_columns':['pol_1','pol_2'],'input_transform':'degrees to radians; no target transformation','target_column':'ir_3','target_unit':'Original recorded sensor output units; no conversion to SI irradiance claimed.','source_archive_md5':hashlib.md5(z.read_bytes()).hexdigest(),'source_archive_sha256':hashlib.sha256(z.read_bytes()).hexdigest(),'measurement_csv_sha256':hashlib.sha256(raw).hexdigest(),'preregistration_sha256':hashlib.sha256(spec.read_bytes()).hexdigest(),'source_identity':lib.cp_source_identity().decode(),'build_identity':lib.cp_build_identity().decode(),'models':results,'selected_by_training_bic':selected,'selected_conformal_radius':radius.value,'selected_holdout_interval_coverage':coverage,'limits':registration['limits']}
(out/'empirical_results.json').write_text(json.dumps(summary,indent=2));np.savetxt(out/'heldout_predictions.csv',np.column_stack((x[800:],y[800:],*[z[800:] for z in predictions])),delimiter=',',header='theta1_rad,theta2_rad,ir_3,pred_template0,pred_template1,pred_linear',comments='',fmt='%.17g');print(json.dumps(summary,indent=2))
