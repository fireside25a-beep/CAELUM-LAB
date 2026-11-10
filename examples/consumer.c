/* Actual public SDK consumer: compile and execute without access to private
 * CAELUM symbols. Produces predictions and exercises Arrow/DLPack ownership. */
#include "caelum_exchange.h"
#include <stdio.h>
#include <math.h>
int main(void){
 const double x[]={0,0,1,0,0,1,1,1,2,-1};
 const double y[]={2,5,0,3,10};
 cp_matrix_view v={x,5,2,2};cp_linear_model m={0};
 if(cp_fit_linear_qr(v,y,&m)!=CP_OK)return 1;
 const double question[]={3,2};double result=0;
 if(cp_predict_linear(&m,question,&result)!=CP_OK)return 2;
 if(fabs(result-7.0)>1e-12)return 3;
 cp_dataset dataset={(double*)x,(double*)y,5,2};
 struct ArrowArray array={0};struct ArrowSchema schema={0};
 if(cp_arrow_export_dataset(&dataset,&array,&schema)!=CP_OK)return 4;
 cp_dataset imported={0};
 if(cp_arrow_import_dataset(&array,&schema,&imported)!=CP_OK)return 5;
 if(imported.features!=2||imported.rows!=5)return 6;
 cp_dataset_release(&imported);array.release(&array);schema.release(&schema);
 cp_dl_managed_tensor_versioned *tensor=NULL;
 if(cp_dlpack_export_features(&dataset,&tensor)!=CP_OK)return 7;
 cp_owned_matrix out={0};
 if(cp_dlpack_import_features(tensor,&out)!=CP_OK)return 8;
 if(out.rows!=5||out.columns!=2)return 9;
 cp_owned_matrix_release(&out);tensor->deleter(tensor);
 printf("SDK consumer: prediction=%g, Arrow/DLPack round trips=PASS\n",result);
 return 0;
}
