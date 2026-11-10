#define _POSIX_C_SOURCE 200809L
#include "caelum_lab.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
/* The workbench observes and invokes the public experiment authority. No
   scientific fitting, selection or update algorithm is duplicated here. */
typedef struct workbench {
 Display *d;Window window;GC gc;int width,height,focus,dirty,thread_valid;
 unsigned long bg,panel,ink,muted,accent,green,red,border;
 char protocol[4096],bundle[4096],message[256];
 atomic_int busy,stop;pthread_t thread;pthread_mutex_t mutex;cp_run_summary summary;
 int operation;cp_status status;
} workbench;
static unsigned long color(Display *d,const char *name){XColor exact,screen;if(!XAllocNamedColor(d,DefaultColormap(d,DefaultScreen(d)),name,&screen,&exact))return BlackPixel(d,DefaultScreen(d));return screen.pixel;}
static void fill(workbench *w,int x,int y,unsigned width,unsigned height,unsigned long c){XSetForeground(w->d,w->gc,c);XFillRectangle(w->d,w->window,w->gc,x,y,width,height);}
static void text(workbench *w,int x,int y,const char *s,unsigned long c){XSetForeground(w->d,w->gc,c);size_t n=strlen(s);if(n>1000)n=1000;XDrawString(w->d,w->window,w->gc,x,y,s,(int)n);}
static const char *phase(unsigned p){static const char *names[]={"CREATED","READY","RUNNING","STOPPED","SEALED","COUNTEREXAMPLE"};return p<6?names[p]:"INVALID";}
static void button(workbench *w,int x,unsigned width,const char *s,int enabled){fill(w,x,150,width,36,enabled?w->accent:w->border);text(w,x+14,173,s,w->ink);}
static void *job(void *arg){
 workbench *w=arg;cp_run_summary summary={0};cp_status status=CP_OK;
 if(w->operation==2)status=cp_lab_run_replay(w->bundle);
 else{
  int resume=w->operation==1;
  do{
   status=cp_lab_run(resume?NULL:w->protocol,w->bundle,1,resume,&summary);
   pthread_mutex_lock(&w->mutex);if(status==CP_OK)w->summary=summary;pthread_mutex_unlock(&w->mutex);
   resume=1;
  }while(status==CP_OK&&summary.phase==CP_RUN_STOPPED&&!atomic_load(&w->stop));
 }
 pthread_mutex_lock(&w->mutex);w->status=status;snprintf(w->message,sizeof(w->message),"%s: %s",w->operation==2?"Replay":"Experiment",cp_status_string(status));pthread_mutex_unlock(&w->mutex);
 atomic_store(&w->busy,0);return NULL;
}
static void start(workbench *w,int operation){
 if(atomic_load(&w->busy))return;
 if(w->thread_valid){pthread_join(w->thread,NULL);w->thread_valid=0;}
 w->operation=operation;atomic_store(&w->stop,0);atomic_store(&w->busy,1);
 snprintf(w->message,sizeof(w->message),"%s",operation==2?"Verifying frozen inputs and replay...":"Running the native experiment authority...");
 if(pthread_create(&w->thread,NULL,job,w)!=0){atomic_store(&w->busy,0);strcpy(w->message,"Could not start worker");}else w->thread_valid=1;
 w->dirty=1;
}
static void plot(workbench *w,cp_run_summary s,int left,int top,int width,int height){
 text(w,left,top-14,"Pre-measurement prediction and observed response",w->ink);
 XSetForeground(w->d,w->gc,w->border);XDrawRectangle(w->d,w->window,w->gc,left,top,(unsigned)width,(unsigned)height);
 cp_run_point points[256];unsigned count=0;double lo=INFINITY,hi=-INFINITY;
 for(unsigned k=0;k<s.steps&&k<256;k++){
  if(cp_lab_run_point(w->bundle,k,points+k)!=CP_OK)break;
  lo=fmin(lo,fmin(points[k].prediction,points[k].observed));hi=fmax(hi,fmax(points[k].prediction,points[k].observed));count++;
 }
 if(!count){text(w,left+24,top+45,"Measurements appear as the experiment advances.",w->muted);return;}
 if(hi==lo){lo-=.5;hi+=.5;}double range=hi-lo;lo-=range*.1;hi+=range*.1;
 int px=0,py=0;
 for(unsigned k=0;k<count;k++){
  int x=left+12+(int)((double)k/(double)(count>1?count-1:1)*(width-24));
  int y=top+height-12-(int)((points[k].prediction-lo)/(hi-lo)*(height-24));
  XSetForeground(w->d,w->gc,w->accent);if(k)XDrawLine(w->d,w->window,w->gc,px,py,x,y);XFillArc(w->d,w->window,w->gc,x-3,y-3,6,6,0,360*64);px=x;py=y;
  y=top+height-12-(int)((points[k].observed-lo)/(hi-lo)*(height-24));XSetForeground(w->d,w->gc,w->green);XFillArc(w->d,w->window,w->gc,x-4,y-4,8,8,0,360*64);
 }
 text(w,left+15,top+height+24,"Blue: prediction before seeing y    Green: measured y",w->muted);
}
static void draw(workbench *w){
 cp_run_summary s;char message[256];cp_status status;
 pthread_mutex_lock(&w->mutex);s=w->summary;memcpy(message,w->message,sizeof(message));status=w->status;pthread_mutex_unlock(&w->mutex);
 int busy=atomic_load(&w->busy),half=w->width/2;
 fill(w,0,0,(unsigned)w->width,(unsigned)w->height,w->bg);
 text(w,28,35,"CAELUM / SCIENTIFIC LABORATORY",w->ink);text(w,28,57,"Native experiment authority  |  imported measurements  |  reproducible evidence",w->muted);
 text(w,28,88,"PROTOCOL",w->muted);fill(w,125,70,(unsigned)(w->width-155),28,w->panel);text(w,135,89,w->protocol,w->ink);
 text(w,28,124,"BUNDLE",w->muted);fill(w,125,106,(unsigned)(w->width-155),28,w->panel);text(w,135,125,w->bundle,w->ink);
 if(w->focus){XSetForeground(w->d,w->gc,w->accent);XDrawRectangle(w->d,w->window,w->gc,124,w->focus==1?69:105,(unsigned)(w->width-153),30);}
 button(w,28,125,"Run [R]",!busy);button(w,167,135,"Resume [N]",!busy);button(w,316,125,"Stop [S]",busy);button(w,455,140,"Replay [P]",!busy);
 text(w,28,211,message,status==CP_OK?w->green:w->red);
 fill(w,28,235,(unsigned)(half-42),(unsigned)(w->height-265),w->panel);fill(w,half+2,235,(unsigned)(half-30),(unsigned)(w->height-265),w->panel);
 text(w,48,265,"EXPERIMENT / EVIDENCE",w->ink);char line[256];
 snprintf(line,sizeof(line),"State: %s%s",phase(s.phase),busy?" (worker active)":"");text(w,48,300,line,w->accent);
 snprintf(line,sizeof(line),"Interventions: %u    Cost: %.6g",s.steps,s.cost);text(w,48,330,line,w->ink);
 snprintf(line,sizeof(line),"Models: %u    Rejected: %u",s.model_count,s.rejected_models);text(w,48,360,line,w->ink);
 snprintf(line,sizeof(line),"Holdout RMSE: %.8g",s.holdout_rmse);text(w,48,390,line,w->ink);
 snprintf(line,sizeof(line),"Model ensemble entropy: %.8g",s.predictive_entropy);text(w,48,420,line,w->ink);
 if(s.causal_status){
  snprintf(line,sizeof(line),"Finite-model effect: %.7g",s.effect);text(w,48,455,line,w->green);
  snprintf(line,sizeof(line),"Model-class support: [%.7g, %.7g]",s.effect_lower,s.effect_upper);text(w,48,485,line,w->ink);
  text(w,48,515,"Conditional on the declared fully observed SCM class.",w->muted);
 }else{
  snprintf(line,sizeof(line),"Split-conformal radius: %.7g",s.interval_radius);text(w,48,455,line,w->ink);
  snprintf(line,sizeof(line),"Holdout interval coverage: %.5g",s.coverage);text(w,48,485,line,w->ink);
  text(w,48,515,"Prediction does not establish causal identification.",w->muted);
 }
 text(w,48,560,"Latest measured actions",w->ink);
 unsigned first=s.steps>6?s.steps-6:0;
 for(unsigned k=first;k<s.steps;k++){
  cp_run_point point;if(cp_lab_run_point(w->bundle,k,&point)!=CP_OK)break;
  snprintf(line,sizeof(line),"%3u  row=%-4u predicted=% .6g  observed=% .6g",k+1,point.row,point.prediction,point.observed);text(w,48,590+(int)(k-first)*25,line,w->muted);
 }
 plot(w,s,half+26,285,half-79,w->height-420);
 text(w,half+26,w->height-76,"Selection, fitting and replay use the same public C core.",w->muted);
 XFlush(w->d);w->dirty=0;
}
int main(int argc,char **argv){
 if(argc!=3){fprintf(stderr,"Usage: caelum-workbench protocol.json new-or-existing-bundle\n");return 2;}
 if(strlen(argv[1])>=4096||strlen(argv[2])>=4096)return 2;
 workbench w={0};strcpy(w.protocol,argv[1]);strcpy(w.bundle,argv[2]);strcpy(w.message,"Ready. Review the protocol, then Run or Resume.");w.width=1360;w.height=850;w.dirty=1;pthread_mutex_init(&w.mutex,NULL);
 w.d=XOpenDisplay(NULL);if(!w.d){fprintf(stderr,"No X11 display is available\n");return 1;}
 w.bg=color(w.d,"#101821");w.panel=color(w.d,"#18232f");w.ink=color(w.d,"#e8edf3");w.muted=color(w.d,"#a0afbe");w.accent=color(w.d,"#347bbd");w.green=color(w.d,"#76d3b3");w.red=color(w.d,"#ff9b91");w.border=color(w.d,"#344352");
 w.window=XCreateSimpleWindow(w.d,DefaultRootWindow(w.d),0,0,(unsigned)w.width,(unsigned)w.height,0,w.border,w.bg);XStoreName(w.d,w.window,"CAELUM Scientific Laboratory");
 XSelectInput(w.d,w.window,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);w.gc=XCreateGC(w.d,w.window,0,NULL);
 XFontStruct *font=XLoadQueryFont(w.d,"9x15");if(!font)font=XLoadQueryFont(w.d,"fixed");if(font)XSetFont(w.d,w.gc,font->fid);
 Atom quit=XInternAtom(w.d,"WM_DELETE_WINDOW",False);XSetWMProtocols(w.d,w.window,&quit,1);XMapWindow(w.d,w.window);
 cp_run_summary initial={0};if(cp_lab_run_summary(w.bundle,&initial)==CP_OK)w.summary=initial;
 int running=1;while(running){
  while(XPending(w.d)){
   XEvent e;XNextEvent(w.d,&e);
   if(e.type==Expose)w.dirty=1;
   else if(e.type==ConfigureNotify){w.width=e.xconfigure.width<1000?1000:e.xconfigure.width;w.height=e.xconfigure.height<750?750:e.xconfigure.height;w.dirty=1;}
   else if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==quit)running=0;
   else if(e.type==ButtonPress){
    int x=e.xbutton.x,y=e.xbutton.y;w.focus=0;
    if(!atomic_load(&w.busy)&&x>=125&&y>=70&&y<=134)w.focus=y<100?1:2;
    if(y>=150&&y<=186){if(x>=28&&x<=153)start(&w,0);else if(x>=167&&x<=302)start(&w,1);else if(x>=316&&x<=441)atomic_store(&w.stop,1);else if(x>=455&&x<=595)start(&w,2);}w.dirty=1;
   }else if(e.type==KeyPress){
    KeySym key;char bytes[32];int n=XLookupString(&e.xkey,bytes,sizeof(bytes),&key,NULL);
    if(w.focus&&!atomic_load(&w.busy)){
     char *field=w.focus==1?w.protocol:w.bundle;size_t len=strlen(field);
     if(key==XK_Escape||key==XK_Return)w.focus=0;
     else if(key==XK_BackSpace&&len)field[len-1]=0;
     else if(n>0&&len+(size_t)n<4096){memcpy(field+len,bytes,(size_t)n);field[len+(size_t)n]=0;}
    }else if(key==XK_r||key==XK_R)start(&w,0);else if(key==XK_n||key==XK_N)start(&w,1);else if(key==XK_s||key==XK_S)atomic_store(&w.stop,1);else if(key==XK_p||key==XK_P)start(&w,2);else if(key==XK_Escape)running=0;
    w.dirty=1;
   }
  }
  if(w.thread_valid&&!atomic_load(&w.busy)){pthread_join(w.thread,NULL);w.thread_valid=0;w.dirty=1;}
  if(atomic_load(&w.busy))w.dirty=1;
  if(w.dirty)draw(&w);
  struct timespec delay={0,100000000};nanosleep(&delay,NULL);
 }
 atomic_store(&w.stop,1);if(w.thread_valid)pthread_join(w.thread,NULL);if(font)XFreeFont(w.d,font);XFreeGC(w.d,w.gc);XDestroyWindow(w.d,w.window);XCloseDisplay(w.d);pthread_mutex_destroy(&w.mutex);return 0;
}
