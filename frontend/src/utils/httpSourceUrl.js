// A catalog file (Pegasus DL+ ".../catalog.json", any name but index.json)
// is kept as entered, query included: it may carry an access token. Anything
// else is a folder and ends with '/'. Mirrors http_source_sanitize().
export function isCatalogFileUrl(url) {
  const path = (url || '').replace(/[?#].*$/, '');
  return /\.json$/i.test(path) && !/\/index\.json$/i.test(path);
}

export function normalizeUrl(raw) {
  let url = (raw || '').trim();
  if (!url) return '';
  if (!/^https?:\/\//i.test(url)) url = `http://${url}`;
  if (isCatalogFileUrl(url)) return url;
  url = url.replace(/[?#].*$/, '').replace(/\/index\.json$/i, '/');
  if (!url.endsWith('/')) url += '/';
  return url;
}
