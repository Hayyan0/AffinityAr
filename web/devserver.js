import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { exec } from 'node:child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const PORT = parseInt(process.env.PORT || '3000', 10);
const HOST = 'localhost';

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.woff': 'font/woff',
  '.ttf': 'font/ttf',
  '.otf': 'font/otf',
  '.webp': 'image/webp',
};

const LIVE_RELOAD_SNIPPET = `
<!-- Live Reload Client -->
<script>
(() => {
  let es;
  function connect() {
    es = new EventSource('/__livereload');
    es.onmessage = (event) => {
      if (event.data === 'reload') {
        console.log('[DevServer] Changes detected, reloading...');
        location.reload();
      }
    };
    es.onerror = () => {
      es.close();
      setTimeout(connect, 1000);
    };
  }
  connect();
  console.log('[DevServer] Live reload active');
})();
</script>
`;

const clients = new Set();

function broadcastReload() {
  console.log(`\x1b[36m[DevServer]\x1b[0m File changed, reloading browser...`);
  for (const res of clients) {
    try {
      res.write(`data: reload\n\n`);
    } catch {
      clients.delete(res);
    }
  }
}

let reloadTimeout = null;
fs.watch(__dirname, { recursive: true }, (eventType, filename) => {
  if (!filename) return;
  // Ignore git or hidden files
  if (filename.startsWith('.') || filename.includes('original')) return;
  
  clearTimeout(reloadTimeout);
  reloadTimeout = setTimeout(() => {
    broadcastReload();
  }, 100);
});

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || HOST}`);
  let pathname = decodeURIComponent(url.pathname);

  if (pathname === '/__livereload') {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      'Connection': 'keep-alive',
      'Access-Control-Allow-Origin': '*',
    });
    res.write(': connected\n\n');
    clients.add(res);

    req.on('close', () => {
      clients.delete(res);
    });
    return;
  }

  if (pathname === '/') {
    pathname = '/index.html';
  }

  let filePath = path.join(__dirname, pathname);

  if (!filePath.startsWith(__dirname)) {
    res.writeHead(403, { 'Content-Type': 'text/plain' });
    res.end('Forbidden');
    return;
  }

  try {
    const stat = fs.statSync(filePath);
    if (stat.isDirectory()) {
      filePath = path.join(filePath, 'index.html');
    }
  } catch {
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(`<h1>404 Not Found</h1><p>${pathname} could not be found.</p>`);
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const mime = MIME_TYPES[ext] || 'application/octet-stream';

    if (ext === '.html') {
      let html = data.toString('utf-8');
      if (html.includes('</body>')) {
        html = html.replace('</body>', `${LIVE_RELOAD_SNIPPET}\n</body>`);
      } else {
        html += LIVE_RELOAD_SNIPPET;
      }
      res.writeHead(200, {
        'Content-Type': mime,
        'Cache-Control': 'no-store',
      });
      res.end(html);
    } else {
      res.writeHead(200, {
        'Content-Type': mime,
        'Cache-Control': 'no-cache',
      });
      res.end(data);
    }
  });
});

server.listen(PORT, HOST, () => {
  const url = `http://${HOST}:${PORT}/`;
  console.log(`
\x1b[32m╔══════════════════════════════════════════════════════════════╗
║              Affinity Arabic — Live Dev Server               ║
╚══════════════════════════════════════════════════════════════╝\x1b[0m

  \x1b[1m➜ Local:\x1b[0m   \x1b[36m${url}\x1b[0m
  \x1b[1m➜ Root:\x1b[0m    ${__dirname}
  \x1b[1m➜ Reload:\x1b[0m  Active (auto-refreshes on file changes)

  Ready for editing \x1b[33mindex.html\x1b[0m & \x1b[33mstyle.css\x1b[0m!
`);

  const openCmd = process.platform === 'win32' ? `start "" "${url}"` : `open "${url}"`;
  exec(openCmd, () => {});
});
