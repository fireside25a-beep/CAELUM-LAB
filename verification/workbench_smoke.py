import argparse,ctypes as C,json,os,pathlib,subprocess,time,socket,fcntl
from PIL import ImageGrab
p=argparse.ArgumentParser();p.add_argument('--xvfb',required=True);p.add_argument('--gui',required=True);p.add_argument('--protocol',required=True);p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,DISPLAY='127.0.0.1:35675')
class Button(C.Structure):
 _fields_=[('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),('display',C.c_void_p),('window',C.c_ulong),('root',C.c_ulong),('subwindow',C.c_ulong),('time',C.c_ulong),('x',C.c_int),('y',C.c_int),('x_root',C.c_int),('y_root',C.c_int),('state',C.c_uint),('button',C.c_uint),('same_screen',C.c_int)]
class Event(C.Union):_fields_=[('button',Button),('pad',C.c_long*24)]
x=C.CDLL('libX11.so.6');x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=C.c_void_p;x.XDefaultRootWindow.argtypes=[C.c_void_p];x.XDefaultRootWindow.restype=C.c_ulong
x.XQueryTree.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.POINTER(C.c_ulong)),C.POINTER(C.c_uint)];x.XFetchName.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(C.c_char_p)];x.XFree.argtypes=[C.c_void_p];x.XSendEvent.argtypes=[C.c_void_p,C.c_ulong,C.c_int,C.c_long,C.POINTER(Event)];x.XFlush.argtypes=[C.c_void_p];x.XCloseDisplay.argtypes=[C.c_void_p]
server_log=(out/'xserver.log').open('w');gui_log=(out/'workbench.log').open('w')
server=subprocess.Popen([str(pathlib.Path(a.xvfb).resolve()),':35675','-screen','0','1440x960x24','-ac','-nolisten','unix','-nolisten','local','-listen','tcp'],stdout=server_log,stderr=subprocess.STDOUT)
gui=None;display=None
try:
 for _ in range(100):
  display=x.XOpenDisplay(env['DISPLAY'].encode())
  if display:break
  if server.poll() is not None:raise RuntimeError('Xvfb failed; see xserver.log')
  time.sleep(.05)
 assert display,'X11 display did not become available'
 bundle=out/('run-'+str(time.time_ns()));gui=subprocess.Popen([str(pathlib.Path(a.gui).resolve()),str(pathlib.Path(a.protocol).resolve()),str(bundle)],env=env,stdout=gui_log,stderr=subprocess.STDOUT)
 root=x.XDefaultRootWindow(display);window=0
 for _ in range(100):
  r=C.c_ulong();parent=C.c_ulong();children=C.POINTER(C.c_ulong)();count=C.c_uint()
  x.XQueryTree(display,root,C.byref(r),C.byref(parent),C.byref(children),C.byref(count))
  for i in range(count.value):
   name=C.c_char_p();x.XFetchName(display,children[i],C.byref(name))
   if name.value==b'CAELUM Scientific Laboratory':window=children[i]
   if name:x.XFree(name)
  if children:x.XFree(children)
  if window:break
  time.sleep(.05)
 assert window,'workbench window not found'
 def click(px,py):
  e=Event();e.button=Button(4,0,1,display,window,root,0,0,px,py,px,py,0,1,1)
  assert x.XSendEvent(display,window,0,1<<2,C.byref(e));x.XFlush(display)
 # Hold a real IO transaction lock while Run and Stop reach the event loop.
 # The worker finishes one atomic step after release, then honors Stop.
 lock_path=f'/tmp/caelum-scientific-io-{os.getuid()}.lock'
 with open(lock_path,'a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX);click(80,165);click(375,165);time.sleep(.25);fcntl.flock(lock,fcntl.LOCK_UN)
 for _ in range(200):
  report=bundle/'result.json'
  if report.exists():
   stopped=json.loads(report.read_text())
   if stopped['phase']==3:break
  time.sleep(.05)
 else:raise RuntimeError('Stop did not checkpoint the native worker')
 assert stopped['steps']==1,stopped
 time.sleep(.2);ImageGrab.grab(xdisplay=env['DISPLAY']).save(out/'workbench_stopped.png')
 click(225,165)
 for _ in range(600):
  report=bundle/'result.json'
  if report.exists():
   result=json.loads(report.read_text())
   if result['phase'] in [4,5]:break
  if gui.poll() is not None:raise RuntimeError('workbench exited early')
  time.sleep(.05)
 else:raise RuntimeError('GUI experiment did not complete')
 time.sleep(.4);ImageGrab.grab(xdisplay=env['DISPLAY']).save(out/'workbench_complete.png')
 click(510,165);time.sleep(1);ImageGrab.grab(xdisplay=env['DISPLAY']).save(out/'workbench_replay.png')
 assert result['phase']==4 and result['steps']==10,result
 (out/'ui_qualification.json').write_text(json.dumps({'status':'PASS','native':'Linux X11','actual_button_events':['Run','Stop','Resume','Replay'],'stopped_steps':stopped['steps'],'window':window,'run_result':result,'scope':'Headless X server with the real native workbench and core; not Windows/macOS execution evidence.'},indent=2))
 x.XKeysymToKeycode.argtypes=[C.c_void_p,C.c_ulong];x.XKeysymToKeycode.restype=C.c_uint
 e=Event();e.button=Button(2,0,1,display,window,root,0,0,0,0,0,0,0,x.XKeysymToKeycode(display,0xff1b),1)
 assert x.XSendEvent(display,window,0,1,C.byref(e));x.XFlush(display)
 assert gui.wait(timeout=5)==0,'Native window did not close cleanly'
 print('PASS actual X11 Run, Stop, Resume, Replay and clean window close; native screenshots saved')
finally:
 if display:x.XCloseDisplay(display)
 if gui:
  gui.terminate()
  try:gui.wait(5)
  except subprocess.TimeoutExpired:gui.kill();gui.wait()
 server.terminate()
 try:server.wait(5)
 except subprocess.TimeoutExpired:server.kill();server.wait()
 server_log.close();gui_log.close()
