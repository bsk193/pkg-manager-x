import { useCallback, useState } from 'react';
import { getHttpSources, saveHttpSources, testHttpSource } from '../api/httpSources';

export const EMPTY_HTTP_FORM = {
  id: '',
  label: '',
  url: '',
  username: '',
  password: '',
  tls_mode: 'verify',
  tls_pin: '',
  enabled: true
};

function normalizeUrl(raw) {
  let url = (raw || '').trim();
  if (!url) return '';
  if (!/^https?:\/\//i.test(url)) url = `http://${url}`;
  url = url.replace(/[?#].*$/, '').replace(/\/index\.json$/i, '/');
  if (!url.endsWith('/')) url += '/';
  return url;
}

export function useHttpSources({ showToast, refreshAll }) {
  const [httpSources, setHttpSources] = useState([]);
  const [showHttpModal, setShowHttpModal] = useState(false);
  const [httpEditIndex, setHttpEditIndex] = useState(-1);
  const [httpForm, setHttpForm] = useState(EMPTY_HTTP_FORM);
  const [httpTesting, setHttpTesting] = useState(false);
  const [httpTestResult, setHttpTestResult] = useState(null);

  const fetchHttpSources = useCallback(async () => {
    try {
      setHttpSources(await getHttpSources());
    } catch (e) {
      // Older backend without HTTP sources: keep the list empty.
    }
  }, []);

  const persist = async (list, successMsg) => {
    try {
      await saveHttpSources(list);
      await fetchHttpSources();
      if (successMsg) showToast(successMsg, 'success');
      if (refreshAll) refreshAll();
      return true;
    } catch (e) {
      showToast(e.message || 'Failed to save HTTP sources', 'error');
      return false;
    }
  };

  const openAddHttpSource = () => {
    setHttpEditIndex(-1);
    setHttpForm(EMPTY_HTTP_FORM);
    setHttpTestResult(null);
    setShowHttpModal(true);
  };

  const openEditHttpSource = (idx, src) => {
    setHttpEditIndex(idx);
    setHttpForm({ ...EMPTY_HTTP_FORM, ...src, password: '' });
    setHttpTestResult(null);
    setShowHttpModal(true);
  };

  const handleSaveHttpSource = async () => {
    const url = normalizeUrl(httpForm.url);
    if (!url) {
      showToast('Server URL is required', 'error');
      return;
    }
    const entry = { ...httpForm, url, label: (httpForm.label || '').trim(), username: (httpForm.username || '').trim() };
    const list = [...httpSources];
    if (httpEditIndex >= 0 && httpEditIndex < list.length) list[httpEditIndex] = entry;
    else list.push(entry);
    if (await persist(list, 'HTTP source saved')) {
      setShowHttpModal(false);
      setHttpTestResult(null);
    }
  };

  const handleRemoveHttpSource = async (idx) => {
    const list = httpSources.filter((_, i) => i !== idx);
    await persist(list, 'HTTP source removed');
  };

  const handleToggleHttpSource = async (idx) => {
    const list = httpSources.map((s, i) => (i === idx ? { ...s, enabled: !s.enabled } : s));
    await persist(list, null);
  };

  const handleTestHttpSource = async (cfg) => {
    const url = normalizeUrl(cfg && cfg.url);
    if (!url) {
      showToast('Server URL is required', 'error');
      return;
    }
    setHttpTesting(true);
    setHttpTestResult(null);
    try {
      const data = await testHttpSource({ ...cfg, url });
      setHttpTestResult(data);
      showToast(data.message || (data.success ? 'Connection successful' : 'Connection failed'),
        data.success ? 'success' : 'error');
    } catch (e) {
      const msg = e && e.message ? e.message : 'Unknown error';
      setHttpTestResult({ success: false, message: msg });
      showToast('Connection test error: ' + msg, 'error');
    } finally {
      setHttpTesting(false);
    }
  };

  // Pins the certificate fingerprint reported by the last test.
  const trustHttpFingerprint = (fingerprint) => {
    setHttpForm((f) => ({ ...f, tls_mode: 'pin', tls_pin: fingerprint }));
    setHttpTestResult(null);
  };

  return {
    httpSources,
    fetchHttpSources,
    showHttpModal,
    setShowHttpModal,
    httpEditIndex,
    httpForm,
    setHttpForm,
    httpTesting,
    httpTestResult,
    openAddHttpSource,
    openEditHttpSource,
    handleSaveHttpSource,
    handleRemoveHttpSource,
    handleToggleHttpSource,
    handleTestHttpSource,
    trustHttpFingerprint
  };
}
