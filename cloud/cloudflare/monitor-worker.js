// Cloudflare Worker: serves the dashboard (GitHub Pages,
// https://noelcav.github.io/Ambient-Monitor/) at https://www.noelcaverly.com/monitor/
// so the address bar stays on noelcaverly.com. Setup in cloud/README.md.
// Route: www.noelcaverly.com/monitor*
const ORIGIN = 'https://noelcav.github.io/Ambient-Monitor';

export default {
  async fetch(request) {
    const url = new URL(request.url);
    // The dashboard's files are relative (common.js, config.js), so the page
    // must be served from /monitor/ with the trailing slash.
    if (url.pathname === '/monitor') return Response.redirect(url.origin + '/monitor/' + url.search, 301);
    const path = url.pathname.slice('/monitor'.length);  // "/", "/common.js", ...
    return fetch(ORIGIN + path + url.search, {cf: {cacheTtl: 300}});
  },
};
