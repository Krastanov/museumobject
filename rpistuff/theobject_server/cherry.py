import json
import os
import sqlite3
import time
import threading
import urllib.request

import cherrypy

starttime = time.time()

conn = sqlite3.connect(':memory:', check_same_thread=False) # TODO proper persistent provisioning and multithreading

with conn:
    conn.execute('CREATE TABLE nodes (mac TEXT PRIMARY KEY, ip TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)')
    conn.execute('CREATE TABLE reports (mac TEXT PRIMARY KEY, type TEXT, report TEXT)') # TODO foreign key

def clumsy_checks(mac, reptype, rep):
    print((mac,reptype,rep))
    if mac == 'DC4F220A92A5' and reptype == 'motion' and rep == 'shaking':
        def toggle(mac_tog):
            print(mac_tog)
            ip = conn.execute('SELECT ip FROM nodes WHERE mac=?', (mac_tog,)).fetchone()[0]
            print(ip)
            print(urllib.request.urlopen('http://%s/pulseled'%ip).read())
        for m in ['DC4F220ABCFF','DC4F220ABF67','DC4F220AC8BD']:
            threading.Thread(target=toggle, args=(m,)).start()

class Root:
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.new_registration = threading.Event()
        self.new_report = threading.Event() # TODO a separate event per mac and per report type

    @cherrypy.expose
    def index(self):
        with open('index.html', 'r') as f:
            main_page = f.read()
            return main_page

    @cherrypy.expose
    def registered(self):
        cherrypy.response.headers["Content-Type"] = "text/event-stream"
        def stream():
            while True:
                with conn:
                    yield 'data:'+json.dumps({mac:{'ip':ip,'t':t} for mac,ip,t in conn.execute('SELECT * FROM nodes').fetchall()})+'\n\n'
                self.new_registration.clear()
                self.new_registration.wait()
        return stream()
    registered._cp_config = {'response.stream': True, 'tools.encode.encoding':'utf-8'}

    @cherrypy.expose
    def register(self, mac=None, ip=None): # TODO proper argument validation
        with conn:
            conn.execute('INSERT OR REPLACE INTO nodes VALUES (?, ?, datetime("now"))', (mac.replace(':',''), ip))
        self.new_registration.set()
        return str(int((time.time()-starttime)*1000)%(3600000*24))

    @cherrypy.expose
    def reported(self, mac, reptype): # TODO use a single stream, not a stream per mac/reptype
        cherrypy.response.headers["Content-Type"] = "text/event-stream"
        def stream():
            while True:
                with conn:
                    rep = conn.execute('SELECT report FROM reports WHERE mac=? AND type=?',
                                       (mac, reptype)).fetchall()
                rep = rep[0][0] if rep else 'no data'
                yield 'data:'+rep+'\n\n'
                self.new_report.clear()
                self.new_report.wait()
        return stream()
    reported._cp_config = {'response.stream': True, 'tools.encode.encoding':'utf-8'}

    @cherrypy.expose
    def report(self, mac=None, reptype=None, rep=None): # TODO proper argument validation
        clumsy_checks(mac.replace(':',''),reptype,rep) # TODO all these mac.replace should be done in a more centralized place
        with conn:
            conn.execute('INSERT OR REPLACE INTO reports VALUES (?, ?, ?)', (mac.replace(':',''), reptype, rep))
        self.new_report.set()
        return 'ok' # TODO proper put semantics


if __name__ == '__main__':
    cherrypy.config.update({'server.socket_host'   : '172.24.1.1',
                            'server.socket_port'   : 80,
                            'server.thread_pool'   : 30,
                            'tools.encode.on'      : True,
                            'log.error_file'       : 'site.log',
                           })
    conf = {'/static':{
                            'tools.staticdir.on'   : True,
                            'tools.staticdir.dir'  : '',
                            'tools.staticdir.root' : os.path.join(os.path.dirname(os.path.realpath(__file__)),'static'),
                      }}
    root = Root()
    cherrypy.tree.mount(root=root, config=conf)
    cherrypy.engine.start()
    cherrypy.engine.block()
