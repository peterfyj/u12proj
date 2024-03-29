# Copyright 2009 The Go Authors. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

# This is the server part of the continuous build system for Go. It must be run
# by AppEngine.

from google.appengine.api import mail
from google.appengine.api import memcache
from google.appengine.runtime import DeadlineExceededError
from google.appengine.ext import db
from google.appengine.ext import webapp
from google.appengine.ext.webapp import template
from google.appengine.ext.webapp.util import run_wsgi_app
import binascii
import datetime
import hashlib
import hmac
import logging
import os
import re
import struct
import time
import bz2

# local imports
import key
import const

# The majority of our state are commit objects. One of these exists for each of
# the commits known to the build system. Their key names are of the form
# <commit number (%08x)> "-" <hg hash>. This means that a sorting by the key
# name is sufficient to order the commits.
#
# The commit numbers are purely local. They need not match up to the commit
# numbers in an hg repo. When inserting a new commit, the parent commit must be
# given and this is used to generate the new commit number. In order to create
# the first Commit object, a special command (/init) is used.
class Commit(db.Model):
    num = db.IntegerProperty() # internal, monotonic counter.
    node = db.StringProperty() # Hg hash
    parentnode = db.StringProperty() # Hg hash
    user = db.StringProperty()
    date = db.DateTimeProperty()
    desc = db.BlobProperty()

    # This is the list of builds. Each element is a string of the form <builder
    # name> "`" <log hash>. If the log hash is empty, then the build was
    # successful.
    builds = db.StringListProperty()

    fail_notification_sent = db.BooleanProperty()

class Benchmark(db.Model):		
    name = db.StringProperty()		
    version = db.IntegerProperty()	

class BenchmarkResults(db.Model):
    builder = db.StringProperty()
    benchmark = db.StringProperty()
    data = db.ListProperty(long)	# encoded as [-1, num, iterations, nsperop]*

class Cache(db.Model):
    data = db.BlobProperty()
    expire = db.IntegerProperty()

# A CompressedLog contains the textual build log of a failed build. 
# The key name is the hex digest of the SHA256 hash of the contents.
# The contents is bz2 compressed.
class CompressedLog(db.Model):
    log = db.BlobProperty()

# For each builder, we store the last revision that it built. So, if it
# crashes, it knows where to start up from. The key names for these objects are
# "hw-" <builder name>
class Highwater(db.Model):
    commit = db.StringProperty()

N = 30

def cache_get(key):
    c = Cache.get_by_key_name(key)
    if c is None or c.expire < time.time():
        return None
    return c.data

def cache_set(key, val, timeout):
    c = Cache(key_name = key)
    c.data = val
    c.expire = int(time.time() + timeout)
    c.put()

def cache_del(key):
    c = Cache.get_by_key_name(key)
    if c is not None:
        c.delete()

def builderInfo(b):
    f = b.split('-', 3)
    goos = f[0]
    goarch = f[1]
    note = ""
    if len(f) > 2:
        note = f[2]
    return {'name': b, 'goos': goos, 'goarch': goarch, 'note': note}

def builderset():
    q = Commit.all()
    q.order('-__key__')
    results = q.fetch(N)
    builders = set()
    for c in results:
        builders.update(set(parseBuild(build)['builder'] for build in c.builds))
    return builders
    
class MainPage(webapp.RequestHandler):
    def get(self):
        self.response.headers['Content-Type'] = 'text/html; charset=utf-8'

        try:
            page = int(self.request.get('p', 1))
            if not page > 0:
                raise
        except:
            page = 1

        try:
            num = int(self.request.get('n', N))
            if num <= 0 or num > 200:
                raise
        except:
            num = N

        offset = (page-1) * num

        q = Commit.all()
        q.order('-__key__')
        results = q.fetch(num, offset)

        revs = [toRev(r) for r in results]
        builders = {}

        for r in revs:
            for b in r['builds']:
                builders[b['builder']] = builderInfo(b['builder'])

        for r in revs:
            have = set(x['builder'] for x in r['builds'])
            need = set(builders.keys()).difference(have)
            for n in need:
                r['builds'].append({'builder': n, 'log':'', 'ok': False})
            r['builds'].sort(cmp = byBuilder)

        builders = list(builders.items())
        builders.sort()
        values = {"revs": revs, "builders": [v for k,v in builders]}

        values['num'] = num
        values['prev'] = page - 1
        if len(results) == num:
            values['next'] = page + 1

        path = os.path.join(os.path.dirname(__file__), 'main.html')
        self.response.out.write(template.render(path, values))

class GetHighwater(webapp.RequestHandler):
    def get(self):
        builder = self.request.get('builder')
        
        key = 'hw-%s' % builder
        node = memcache.get(key)
        if node is None:
            hw = Highwater.get_by_key_name('hw-%s' % builder)
            if hw is None:
                # If no highwater has been recorded for this builder,
                # we go back N+1 commits and return that.
                q = Commit.all()
                q.order('-__key__')
                c = q.fetch(N+1)[-1]
                node = c.node
            else:
                # if the proposed hw is too old, bump it forward
                node = hw.commit
                found = False
                q = Commit.all()
                q.order('-__key__')
                recent = q.fetch(N+1)
                for c in recent:
                    if c.node == node:
                        found = True
                        break
                if not found:
                    node = recent[-1].node
            memcache.set(key, node, 3600)
        self.response.set_status(200)
        self.response.out.write(node)

def auth(req):
    k = req.get('key')
    return k == hmac.new(key.accessKey, req.get('builder')).hexdigest() or k == key.accessKey
    
class SetHighwater(webapp.RequestHandler):
    def post(self):
        if not auth(self.request):
            self.response.set_status(403)
            return

        builder = self.request.get('builder')
        newhw = self.request.get('hw')
        q = Commit.all()
        q.filter('node =', newhw)
        c = q.get()
        if c is None:
            self.response.set_status(404)
            return
        
        # if the proposed hw is too old, bump it forward
        found = False
        q = Commit.all()
        q.order('-__key__')
        recent = q.fetch(N+1)
        for c in head:
            if c.node == newhw:
                found = True
                break
        if not found:
            c = recent[-1]

        key = 'hw-%s' % builder
        memcache.delete(key)
        hw = Highwater(key_name = key)
        hw.commit = c.node
        hw.put()

class LogHandler(webapp.RequestHandler):
    def get(self):
        self.response.headers['Content-Type'] = 'text/plain; charset=utf-8'
        hash = self.request.path[5:]
        l = CompressedLog.get_by_key_name(hash)
        if l is None:
            self.response.set_status(404)
            return
        log = bz2.decompress(l.log)
        self.response.set_status(200)
        self.response.out.write(log)

# Init creates the commit with id 0. Since this commit doesn't have a parent,
# it cannot be created by Build.
class Init(webapp.RequestHandler):
    def post(self):
        if not auth(self.request):
            self.response.set_status(403)
            return

        date = parseDate(self.request.get('date'))
        node = self.request.get('node')
        if not validNode(node) or date is None:
            logging.error("Not valid node ('%s') or bad date (%s %s)", node, date, self.request.get('date'))
            self.response.set_status(500)
            return

        commit = Commit(key_name = '00000000-%s' % node)
        commit.num = 0
        commit.node = node
        commit.parentnode = ''
        commit.user = self.request.get('user').encode('utf8')
        commit.date = date
        commit.desc = self.request.get('desc').encode('utf8')

        commit.put()

        self.response.set_status(200)

# Build is the main command: it records the result of a new build.
class Build(webapp.RequestHandler):
    def post(self):
        if not auth(self.request):
            self.response.set_status(403)
            return

        builder = self.request.get('builder')
        log = self.request.get('log').encode('utf-8')

        loghash = ''
        if len(log) > 0:
            loghash = hashlib.sha256(log).hexdigest()
            l = CompressedLog(key_name=loghash)
            l.log = bz2.compress(log)
            l.put()

        date = parseDate(self.request.get('date'))
        user = self.request.get('user').encode('utf8')
        desc = self.request.get('desc').encode('utf8')
        node = self.request.get('node')
        parenthash = self.request.get('parent')
        if not validNode(node) or not validNode(parenthash) or date is None:
            logging.error("Not valid node ('%s') or bad date (%s %s)", node, date, self.request.get('date'))
            self.response.set_status(500)
            return

        q = Commit.all()
        q.filter('node =', parenthash)
        parent = q.get()
        if parent is None:
            logging.error('Cannot find parent %s of node %s' % (parenthash, node))
            self.response.set_status(404)
            return
        parentnum, _ = parent.key().name().split('-', 1)
        nodenum = int(parentnum, 16) + 1

        key_name = '%08x-%s' % (nodenum, node)

        def add_build():
            n = Commit.get_by_key_name(key_name)
            if n is None:
                n = Commit(key_name = key_name)
                n.num = nodenum
                n.node = node
                n.parentnode = parenthash
                n.user = user
                n.date = date
                n.desc = desc
            s = '%s`%s' % (builder, loghash)
            for i, b in enumerate(n.builds):
                if b.split('`', 1)[0] == builder:
                    n.builds[i] = s
                    break
            else:
                n.builds.append(s)
            n.put()

        db.run_in_transaction(add_build)

        key = 'hw-%s' % builder
        hw = Highwater.get_by_key_name(key)
        if hw is None:
            hw = Highwater(key_name = key)
        hw.commit = node
        hw.put()
        memcache.delete(key)
        memcache.delete('hw')

        def mark_sent():
            n = Commit.get_by_key_name(key_name)
            n.fail_notification_sent = True
            n.put()

        n = Commit.get_by_key_name(key_name)
        if loghash and not failed(parent, builder) and not n.fail_notification_sent:
            subject = const.mail_fail_subject % (builder, desc.split("\n")[0])
            path = os.path.join(os.path.dirname(__file__), 'fail-notify.txt')
            body = template.render(path, {
                "builder": builder,
                "node": node[:12],
                "user": user,
                "desc": desc, 
                "loghash": loghash
            })
            mail.send_mail(
                sender=const.mail_from,
                reply_to=const.mail_fail_reply_to,
                to=const.mail_fail_to,
                subject=subject,
                body=body
            )
            db.run_in_transaction(mark_sent)

        self.response.set_status(200)

def failed(c, builder):
    for i, b in enumerate(c.builds):
        p = b.split('`', 1)
        if p[0] == builder:
            return len(p[1]) > 0
    return False

class Benchmarks(webapp.RequestHandler):
    def json(self):
        q = Benchmark.all()
        q.filter('__key__ >', Benchmark.get_or_insert('v002.').key())
        bs = q.fetch(10000)

        self.response.set_status(200)
        self.response.headers['Content-Type'] = 'text/plain; charset=utf-8'
        self.response.out.write('{"benchmarks": [')

        first = True
        sep = "\n\t"
        for b in bs:
            self.response.out.write('%s"%s"' % (sep, b.name))
            sep = ",\n\t"
        self.response.out.write('\n]}\n')

    def get(self):
        if self.request.get('fmt') == 'json':
            return self.json()

        self.response.set_status(200)
        self.response.headers['Content-Type'] = 'text/html; charset=utf-8'
        page = memcache.get('bench')
        if not page:
            # use datastore as cache to avoid computation even
            # if memcache starts dropping things on the floor
            logging.error("memcache dropped bench")
            page = cache_get('bench')
            if not page:
                logging.error("cache dropped bench")
                num = memcache.get('hw')
                if num is None:
                    q = Commit.all()
                    q.order('-__key__')
                    n = q.fetch(1)[0]
                    num = n.num
                    memcache.set('hw', num)
                page = self.compute(num)
                cache_set('bench', page, 600)
            memcache.set('bench', page, 600)
        self.response.out.write(page)

    def compute(self, num):
        benchmarks, builders = benchmark_list()

        rows = []
        for bm in benchmarks:
            row = {'name':bm, 'builders': []}
            for bl in builders:
                key = "single-%s-%s" % (bm, bl)
                url = memcache.get(key)
                row['builders'].append({'name': bl, 'url': url})
            rows.append(row)

        path = os.path.join(os.path.dirname(__file__), 'benchmarks.html')
        data = {
            "builders": [builderInfo(b) for b in builders],
            "rows": rows,
        }
        return template.render(path, data)

    def post(self):
        if not auth(self.request):
            self.response.set_status(403)
            return

        builder = self.request.get('builder')
        node = self.request.get('node')
        if not validNode(node):
            logging.error("Not valid node ('%s')", node)
            self.response.set_status(500)
            return

        benchmarkdata = self.request.get('benchmarkdata')
        benchmarkdata = binascii.a2b_base64(benchmarkdata)

        def get_string(i):
            l, = struct.unpack('>H', i[:2])
            s = i[2:2+l]
            if len(s) != l:
                return None, None
            return s, i[2+l:]

        benchmarks = {}
        while len(benchmarkdata) > 0:
            name, benchmarkdata = get_string(benchmarkdata)
            iterations_str, benchmarkdata = get_string(benchmarkdata)
            time_str, benchmarkdata = get_string(benchmarkdata)
            iterations = int(iterations_str)
            time = int(time_str)

            benchmarks[name] = (iterations, time)

        q = Commit.all()
        q.filter('node =', node)
        n = q.get()
        if n is None:
            logging.error('Client asked for unknown commit while uploading benchmarks')
            self.response.set_status(404)
            return

        for (benchmark, (iterations, time)) in benchmarks.items():
            b = Benchmark.get_or_insert('v002.' + benchmark.encode('base64'), name = benchmark, version = 2)
            key = '%s;%s' % (builder, benchmark)
            r1 = BenchmarkResults.get_by_key_name(key)
            if r1 is not None and (len(r1.data) < 4 or r1.data[-4] != -1 or r1.data[-3] != n.num):
                r1.data += [-1L, long(n.num), long(iterations), long(time)]
                r1.put()            
            key = "bench(%s,%s,%d)" % (benchmark, builder, n.num)
            memcache.delete(key)

        self.response.set_status(200)

class SingleBenchmark(webapp.RequestHandler):
    """
    Fetch data for single benchmark/builder combination 
    and return sparkline url as HTTP redirect, also set memcache entry.
    """
    def get(self):
        benchmark = self.request.get('benchmark')
        builder = self.request.get('builder')
        key = "single-%s-%s" % (benchmark, builder)

        url = memcache.get(key)

        if url is None:
            minr, maxr, bybuilder = benchmark_data(benchmark)
            for bb in bybuilder:
                if bb[0] != builder:
                    continue
                url = benchmark_sparkline(bb[2])

        if url is None:
            self.response.set_status(500, "No data found")
            return

        memcache.set(key, url, 700) # slightly longer than bench timeout 

        self.response.set_status(302)
        self.response.headers.add_header("Location", url)

def node(num):
    q = Commit.all()
    q.filter('num =', num)
    n = q.get()
    return n

def benchmark_data(benchmark):
    q = BenchmarkResults.all()
    q.order('__key__')
    q.filter('benchmark =', benchmark)
    results = q.fetch(100)

    minr = 100000000
    maxr = 0
    for r in results:
        if r.benchmark != benchmark:
            continue
        # data is [-1, num, iters, nsperop, -1, num, iters, nsperop, ...]
        d = r.data
        if not d:
            continue
        if [x for x in d[::4] if x != -1]:
            # unexpected data framing
            logging.error("bad framing for data in %s;%s" % (r.builder, r.benchmark))
            continue
        revs = d[1::4]
        minr = min(minr, min(revs))
        maxr = max(maxr, max(revs))
    if minr > maxr:
        return 0, 0, []

    bybuilder = []
    for r in results:
        if r.benchmark != benchmark:
            continue
        d = r.data
        if not d:
            continue
        nsbyrev = [-1 for x in range(minr, maxr+1)]
        iterbyrev = [-1 for x in range(minr, maxr+1)]
        for num, iter, ns in zip(d[1::4], d[2::4], d[3::4]):
            iterbyrev[num - minr] = iter
            nsbyrev[num - minr] = ns
        bybuilder.append((r.builder, iterbyrev, nsbyrev))

    return minr, maxr, bybuilder

def benchmark_graph(builder, minhash, maxhash, ns):
    valid = [x for x in ns if x >= 0]
    if not valid:
        return ""
    m = max(max(valid), 2*sum(valid)/len(valid))
    s = ""
    encoding = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-"
    for val in ns:
        if val < 0:
            s += "__"
            continue
        val = int(val*4095.0/m)
        s += encoding[val/64] + encoding[val%64]
    return ("http://chart.apis.google.com/chart?cht=lc&chxt=x,y&chxl=0:|%s|%s|1:|0|%g ns|%g ns&chd=e:%s" %
        (minhash[0:12], maxhash[0:12], m/2, m, s))

def benchmark_sparkline(ns):
    valid = [x for x in ns if x >= 0]
    if not valid:
        return ""
    m = max(max(valid), 2*sum(valid)/len(valid))
    # Encoding is 0-61, which is fine enough granularity for our tiny graphs.  _ means missing.
    encoding = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    s = ''.join([x < 0 and "_" or encoding[int((len(encoding)-1)*x/m)] for x in ns])
    url = "http://chart.apis.google.com/chart?cht=ls&chd=s:"+s+"&chs=80x20&chf=bg,s,00000000&chco=000000ff&chls=1,1,0"
    return url

def benchmark_list():
    q = BenchmarkResults.all()
    q.order('__key__')
    q.filter('builder = ', u'darwin-amd64')
    benchmarks = [r.benchmark for r in q]
    
    q = BenchmarkResults.all()
    q.order('__key__')
    q.filter('benchmark =', u'math_test.BenchmarkSqrt')
    builders = [r.builder for r in q.fetch(20)]
    
    return benchmarks, builders
    
class GetBenchmarks(webapp.RequestHandler):
    def get(self):
        benchmark = self.request.path[12:]
        minr, maxr, bybuilder = benchmark_data(benchmark)
        minhash = node(minr).node
        maxhash = node(maxr).node

        if self.request.get('fmt') == 'json':
            self.response.headers['Content-Type'] = 'text/plain; charset=utf-8'
            self.response.out.write('{ "min": "%s", "max": "%s", "data": {' % (minhash, maxhash))
            sep = "\n\t"
            for builder, iter, ns in bybuilder:
                self.response.out.write('%s{ "builder": "%s", "iterations": %s, "nsperop": %s }' %
                    (sep, builder, str(iter).replace("L", ""), str(ns).replace("L", "")))
                sep = ",\n\t"
            self.response.out.write('\n}\n')
            return
        
        graphs = []
        for builder, iter, ns in bybuilder:
            graphs.append({"builder": builder, "url": benchmark_graph(builder, minhash, maxhash, ns)})

        revs = []
        for i in range(minr, maxr+1):
            r = nodeInfo(node(i))
            x = []
            for _, _, ns in bybuilder:
                t = ns[i - minr]
                if t < 0:
                    t = None
                x.append(t)
            r["ns_by_builder"] = x
            revs.append(r)
        revs.reverse()  # same order as front page
        
        path = os.path.join(os.path.dirname(__file__), 'benchmark1.html')
        data = {
            "benchmark": benchmark,
            "builders": [builderInfo(b) for b,_,_ in bybuilder],
            "graphs": graphs,
            "revs": revs,
        }
        self.response.out.write(template.render(path, data))
        
        
class FixedOffset(datetime.tzinfo):
    """Fixed offset in minutes east from UTC."""

    def __init__(self, offset):
        self.__offset = datetime.timedelta(seconds = offset)

    def utcoffset(self, dt):
        return self.__offset

    def tzname(self, dt):
        return None

    def dst(self, dt):
        return datetime.timedelta(0)

def validNode(node):
    if len(node) != 40:
        return False
    for x in node:
        o = ord(x)
        if (o < ord('0') or o > ord('9')) and (o < ord('a') or o > ord('f')):
            return False
    return True

def parseDate(date):
    if '-' in date:
        (a, offset) = date.split('-', 1)
        try:
            return datetime.datetime.fromtimestamp(float(a), FixedOffset(0-int(offset)))
        except ValueError:
            return None
    if '+' in date:
        (a, offset) = date.split('+', 1)
        try:
            return datetime.datetime.fromtimestamp(float(a), FixedOffset(int(offset)))
        except ValueError:
            return None
    try:
        return datetime.datetime.utcfromtimestamp(float(date))
    except ValueError:
        return None

email_re = re.compile('^[^<]+<([^>]*)>$')

def toUsername(user):
    r = email_re.match(user)
    if r is None:
        return user
    email = r.groups()[0]
    return email.replace('@golang.org', '')

def dateToShortStr(d):
    return d.strftime('%a %b %d %H:%M')

def parseBuild(build):
    [builder, logblob] = build.split('`')
    return {'builder': builder, 'log': logblob, 'ok': len(logblob) == 0}

def nodeInfo(c):
    return {
        "node": c.node,
        "user": toUsername(c.user),
        "date": dateToShortStr(c.date),
        "desc": c.desc,
        "shortdesc": c.desc.split('\n', 2)[0]
    }

def toRev(c):
    b = nodeInfo(c)
    b['builds'] = [parseBuild(build) for build in c.builds]
    return b

def byBuilder(x, y):
    return cmp(x['builder'], y['builder'])

# This is the URL map for the server. The first three entries are public, the
# rest are only used by the builders.
application = webapp.WSGIApplication(
                                     [('/', MainPage),
                                      ('/log/.*', LogHandler),
                                      ('/hw-get', GetHighwater),
                                      ('/hw-set', SetHighwater),

                                      ('/init', Init),
                                      ('/build', Build),
                                      ('/benchmarks', Benchmarks),
                                      ('/benchmarks/single', SingleBenchmark),
                                      ('/benchmarks/.*', GetBenchmarks),
                                     ], debug=True)

def main():
    run_wsgi_app(application)

if __name__ == "__main__":
    main()

