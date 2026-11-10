#include "caelum_lab.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
static int compare_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
cp_status cp_conformal_radius(const double *residuals,size_t n,double alpha,double *radius){
 if(!residuals||!radius||!n||n>1000000||!isfinite(alpha)||alpha<=0||alpha>=1)return CP_INVALID;
 for(size_t i=0;i<n;i++)if(!isfinite(residuals[i])||residuals[i]<0)return CP_INVALID;
 long double rank=ceill(((long double)n+1)*(1-(long double)alpha));
 if(rank>(long double)n)return CP_UNAVAILABLE;
 if(rank<1)return CP_NUMERIC;
 double *sorted=malloc(n*sizeof(double));if(!sorted)return CP_NO_MEMORY;
 for(size_t i=0;i<n;i++)sorted[i]=residuals[i];
 qsort(sorted,n,sizeof(double),compare_double);double result=sorted[(size_t)rank-1];free(sorted);*radius=result;return CP_OK;
}
