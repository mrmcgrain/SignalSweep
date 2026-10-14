"""Evaluate a test expression in this project's USB-connected debug WebView.
Requires adb forward tcp:9229 localabstract:webview_devtools_remote_<app PID>.
"""
import json,sys,urllib.request
import websocket
pages=json.load(urllib.request.urlopen('http://127.0.0.1:9229/json'))
page=next(p for p in pages if p['url'].startswith('https://localhost'))
ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=45,suppress_origin=True)
ws.send(json.dumps({'id':1,'method':'Runtime.evaluate','params':{'expression':sys.argv[1],'awaitPromise':True,'returnByValue':True}}))
while True:
    r=json.loads(ws.recv())
    if r.get('id')==1:
        print(json.dumps(r.get('result',r),indent=2));break
ws.close()
