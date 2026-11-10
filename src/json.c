#include "json.h"
#include "locale_numeric.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
static void space(cp_json *j){while(j->at<j->size&&(j->text[j->at]==' '||j->text[j->at]=='\n'||j->text[j->at]=='\r'||j->text[j->at]=='\t'))j->at++;}
static int hex_digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
cp_status cp_json_string(const cp_json *j,unsigned token,char *out,size_t capacity){
 if(!j||!out||!capacity||token>=j->count||j->tokens[token].type!='s')return CP_INVALID;
 cp_json_token t=j->tokens[token];size_t used=0;
 for(size_t i=t.begin+1;i<t.end-1;i++){
  unsigned char c=(unsigned char)j->text[i];
  if(c=='\\'){
   c=(unsigned char)j->text[++i];
   switch(c){case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
    case 'u':{
     unsigned code=0;for(unsigned k=0;k<4;k++){int digit=hex_digit(j->text[++i]);if(digit<0)return CP_CORRUPT;code=code*16+(unsigned)digit;}
     if(code==0||(code>=0xd800&&code<=0xdfff))return CP_UNSUPPORTED;
     if(code<128)c=(unsigned char)code;
     else{
      if(used+3>=capacity)return CP_RANGE;
      if(code<2048){out[used++]=(char)(0xc0u|(code>>6));out[used++]=(char)(0x80u|(code&63u));}
      else{out[used++]=(char)(0xe0u|(code>>12));out[used++]=(char)(0x80u|((code>>6)&63u));out[used++]=(char)(0x80u|(code&63u));}
      continue;
     }break;
    }
    case '"':case '\\':case '/':break;default:return CP_CORRUPT;
   }
  }
  if(!c||used+1>=capacity)return CP_RANGE;
  out[used++]=(char)c;
 }
 out[used]=0;return CP_OK;
}
static cp_status value(cp_json *j,unsigned depth){
 if(depth>32||j->count>=CP_JSON_TOKENS)return CP_RANGE;
 space(j);if(j->at>=j->size)return CP_CORRUPT;
 unsigned here=j->count++;cp_json_token *t=j->tokens+here;memset(t,0,sizeof(*t));t->begin=j->at;
 char c=j->text[j->at++];
 if(c=='{'||c=='['){
  t->type=c;char close=c=='{'?'}':']';space(j);
  if(j->at<j->size&&j->text[j->at]==close)j->at++;
  else for(;;){
   space(j);unsigned key=j->count;
   if(c=='{'&&(j->at>=j->size||j->text[j->at]!='"'))return CP_CORRUPT;
   cp_status status=value(j,depth+1);if(status!=CP_OK)return status;t->children++;
   if(c=='{'){
    char name[512];status=cp_json_string(j,key,name,sizeof(name));if(status!=CP_OK)return status;
    unsigned prior=here+1;
    while(prior<key){char other[512];status=cp_json_string(j,prior,other,sizeof(other));if(status!=CP_OK)return status;if(!strcmp(name,other))return CP_CORRUPT;prior=j->tokens[j->tokens[prior].next].next;}
    space(j);if(j->at>=j->size||j->text[j->at++]!=':')return CP_CORRUPT;
    status=value(j,depth+1);if(status!=CP_OK)return status;t->children++;
   }
   space(j);if(j->at>=j->size)return CP_CORRUPT;
   c=j->text[j->at++];if(c==close)break;if(c!=',')return CP_CORRUPT;
   c=t->type;
  }
 }else if(c=='"'){
  t->type='s';int closed=0;
  while(j->at<j->size){unsigned char ch=(unsigned char)j->text[j->at++];if(ch=='"'){closed=1;break;}if(ch<32)return CP_CORRUPT;
   if(ch=='\\'){
    if(j->at>=j->size)return CP_CORRUPT;
    char esc=j->text[j->at++];if(esc=='u'){for(unsigned k=0;k<4;k++){if(j->at>=j->size||hex_digit(j->text[j->at++])<0)return CP_CORRUPT;}}
    else if(!strchr("\"\\/bfnrt",esc))return CP_CORRUPT;
   }
  }
  if(!closed)return CP_CORRUPT;
 }else if(c=='t'||c=='f'||c=='n'){
  const char *literal=c=='t'?"true":c=='f'?"false":"null";size_t n=strlen(literal);
  if(t->begin+n>j->size||memcmp(j->text+t->begin,literal,n))return CP_CORRUPT;
  j->at=t->begin+n;t->type=c=='n'?'z':'b';
 }else{
  j->at=t->begin;t->type='n';if(j->text[j->at]=='-')j->at++;
  if(j->at>=j->size)return CP_CORRUPT;
  if(j->text[j->at]=='0')j->at++;
  else{if(j->text[j->at]<'1'||j->text[j->at]>'9')return CP_CORRUPT;while(j->at<j->size&&isdigit((unsigned char)j->text[j->at]))j->at++;}
  if(j->at<j->size&&j->text[j->at]=='.'){j->at++;size_t start=j->at;while(j->at<j->size&&isdigit((unsigned char)j->text[j->at]))j->at++;if(j->at==start)return CP_CORRUPT;}
  if(j->at<j->size&&(j->text[j->at]=='e'||j->text[j->at]=='E')){j->at++;if(j->at<j->size&&(j->text[j->at]=='+'||j->text[j->at]=='-'))j->at++;size_t start=j->at;while(j->at<j->size&&isdigit((unsigned char)j->text[j->at]))j->at++;if(j->at==start)return CP_CORRUPT;}
 }
 t->end=j->at;t->next=j->count;return CP_OK;
}
static int valid_utf8(const char *text,size_t n){
 for(size_t i=0;i<n;i++){
  unsigned c=(unsigned char)text[i];if(c<128)continue;
  unsigned count=0,lower=128,upper=191;
  if(c>=194&&c<=223)count=1;
  else if(c>=224&&c<=239){count=2;if(c==224)lower=160;if(c==237)upper=159;}
  else if(c>=240&&c<=244){count=3;if(c==240)lower=144;if(c==244)upper=143;}
  else return 0;
  if(n-i-1<count)return 0;
  unsigned next=(unsigned char)text[++i];if(next<lower||next>upper)return 0;
  for(unsigned k=1;k<count;k++){next=(unsigned char)text[++i];if(next<128||next>191)return 0;}
 }
 return 1;
}
cp_status cp_json_parse(cp_json *j,const char *text,size_t size){
 if(!j||!text||!size||size>262144||memchr(text,0,size))return CP_INVALID;
 if(!valid_utf8(text,size))return CP_CORRUPT;
 memset(j,0,sizeof(*j));j->text=text;j->size=size;cp_status status=value(j,0);space(j);
 return status==CP_OK&&j->at!=size?CP_CORRUPT:status;
}
int cp_json_get(const cp_json *j,unsigned object,const char *key){
 if(!j||!key||object>=j->count||j->tokens[object].type!='{')return -1;
 for(unsigned i=object+1;i<j->tokens[object].next;){char name[512];if(cp_json_string(j,i,name,sizeof(name))!=CP_OK)return -1;unsigned val=j->tokens[i].next;if(!strcmp(name,key))return (int)val;i=j->tokens[val].next;}
 return -1;
}
int cp_json_at(const cp_json *j,unsigned array,unsigned index){
 if(!j||array>=j->count||j->tokens[array].type!='[')return -1;
 unsigned i=array+1;while(index--&&i<j->tokens[array].next)i=j->tokens[i].next;
 return i<j->tokens[array].next?(int)i:-1;
}
cp_status cp_json_number(const cp_json *j,unsigned token,double *out){
 if(!j||!out||token>=j->count||j->tokens[token].type!='n')return CP_INVALID;
 size_t n=j->tokens[token].end-j->tokens[token].begin;if(n>=128)return CP_RANGE;
 char text[128];memcpy(text,j->text+j->tokens[token].begin,n);text[n]=0;errno=0;char *end;double v=cp_strtod_c(text,&end);
 if(end==text||*end||!isfinite(v)||(errno==ERANGE&&v==0))return CP_NUMERIC;
 *out=v;return CP_OK;
}
cp_status cp_json_boolean(const cp_json *j,unsigned token,int *out){
 if(!j||!out||token>=j->count||j->tokens[token].type!='b')return CP_INVALID;
 *out=j->text[j->tokens[token].begin]=='t';return CP_OK;
}
cp_status cp_json_keys(const cp_json *j,unsigned object,const char *const *keys,unsigned n){
 if(!j||object>=j->count||j->tokens[object].type!='{')return CP_INVALID;
 for(unsigned i=object+1;i<j->tokens[object].next;){char name[512];cp_status status=cp_json_string(j,i,name,sizeof(name));if(status!=CP_OK)return status;int found=0;for(unsigned k=0;k<n;k++)if(!strcmp(name,keys[k]))found=1;if(!found)return CP_UNSUPPORTED;i=j->tokens[j->tokens[i].next].next;}
 return CP_OK;
}
