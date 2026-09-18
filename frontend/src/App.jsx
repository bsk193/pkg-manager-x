import React, { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { QRCodeSVG } from 'qrcode.react';
import BlurIcon, { iconUrlFor } from './BlurIcon';

function formatBytes(bytes) {
  const b = Number(bytes);
  if (!b || b <= 0 || !isFinite(b)) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.min(Math.floor(Math.log(b) / Math.log(k)), sizes.length - 1);
  return parseFloat((b / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function formatEta(seconds) {
  if (!seconds || seconds <= 0 || !isFinite(seconds)) return null;
  const s = Math.ceil(seconds);
  if (s < 60) return `${s}s remaining`;
  if (s < 3600) {
    const mins = Math.floor(s / 60);
    const secs = s % 60;
    return `${mins}m ${secs}s remaining`;
  }
  const hrs = Math.floor(s / 3600);
  const mins = Math.floor((s % 3600) / 60);
  return `${hrs}h ${mins}m remaining`;
}

function formatVersion(ver) {
  if (!ver) return '';
  const clean = String(ver).trim().replace(/^v+/i, '');
  return clean ? `v${clean}` : '';
}

const BUILD_VERSION = typeof __APP_VERSION__ !== 'undefined' ? __APP_VERSION__ : '1.1.0';
const BUILD_COMMIT = typeof __APP_COMMIT__ !== 'undefined' ? __APP_COMMIT__ : '';
const BUILD_DATE = typeof __APP_BUILD_DATE__ !== 'undefined' ? __APP_BUILD_DATE__ : '';

function getBrowserTitle(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager v${v} (${BUILD_COMMIT}, ${BUILD_DATE}) by PLK`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager v${v} (${match[1]}, ${match[2]}) by PLK`;
    }
  }
  return `PKG Manager v${v} by PLK`;
}

function getFullVersion(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager v${v} (${BUILD_COMMIT}, ${BUILD_DATE})`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager v${v} (${match[1]}, ${match[2]})`;
    }
  }
  if (BUILD_COMMIT) {
    return `PKG Manager v${v} (${BUILD_COMMIT})`;
  }
  return `PKG Manager v${v}`;
}

const DONATE_URL = 'https://github.com/itsPLK/ps5-pkg-manager/blob/main/DONATE.md';
const isPlayStation = typeof navigator !== 'undefined' && /PlayStation/i.test(navigator.userAgent);
const DONATE_MODAL_STORAGE_KEY = 'pkgmgr_donate_popup';
const DONATE_MODAL_INTERVAL_MS = 7 * 24 * 60 * 60 * 1000; // 7 days

const ALL_SOURCES_DRIVE = {
  id: '__all__',
  label: 'All Sources',
  path: 'All storage media',
  type: 'all',
  clickable: true
};

function getSourceInfo(pkgPath, drivesList = []) {
  if (!pkgPath) return { id: 'unknown', name: 'Unknown', type: 'unknown' };

  // Check USB: /mnt/usb0 .. /mnt/usb7
  const usbMatch = pkgPath.match(/^\/mnt\/usb(\d+)/i);
  if (usbMatch) {
    const num = usbMatch[1];
    return {
      id: `usb${num}`,
      name: `USB${num}`,
      type: 'usb'
    };
  }

  // Check Disc: /mnt/disc
  if (pkgPath.match(/^\/mnt\/disc/i)) {
    return {
      id: 'disc',
      name: 'Disc',
      type: 'disc'
    };
  }

  // Check SMB: smb://...
  if (pkgPath.startsWith('smb://')) {
    let matchedDrive = null;
    if (Array.isArray(drivesList)) {
      matchedDrive = drivesList.find((d) => {
        if (d.type !== 'smb' || !d.path) return false;
        const normDPath = d.path.replace(/\/+$/, '');
        const normPkg = pkgPath.replace(/\/+$/, '');
        return normPkg === normDPath || normPkg.startsWith(normDPath + '/');
      });
    }

    let rawName = '';
    if (matchedDrive) {
      rawName = (matchedDrive.label || '').replace(/^SMB:\s*/i, '').trim();
    }

    if (!rawName) {
      try {
        const withoutProto = pkgPath.replace(/^smb:\/\//, '');
        const parts = withoutProto.split('/');
        if (parts.length > 1 && parts[1]) {
          rawName = decodeURIComponent(parts[1]);
        } else if (parts.length > 0 && parts[0]) {
          rawName = decodeURIComponent(parts[0]);
        }
      } catch (e) {}
    }

    if (!rawName) rawName = 'SMB';

    const MAX_SMB_LEN = 11;
    let displayName = rawName;
    if (displayName.length > MAX_SMB_LEN) {
      displayName = displayName.slice(0, MAX_SMB_LEN - 3).trim() + '...';
    }

    const driveId = matchedDrive && matchedDrive.id ? matchedDrive.id : `smb_${rawName.toLowerCase()}`;
    return {
      id: driveId,
      name: displayName,
      type: 'smb'
    };
  }

  const mntMatch = pkgPath.match(/^\/mnt\/([^\/]+)/i);
  if (mntMatch) {
    let name = mntMatch[1].toUpperCase();
    if (name.length > 11) name = name.slice(0, 8) + '...';
    return { id: mntMatch[1].toLowerCase(), name, type: 'other' };
  }

  return { id: 'other', name: 'Other', type: 'other' };
}

export default function App() {
  const [isOffline, setIsOffline] = useState(false);
  const [appVersion, setAppVersion] = useState('');
  const [drives, setDrives] = useState([]);
  const [selectedDrive, setSelectedDrive] = useState(() => {
    try {
      const saved = localStorage.getItem('pkgmgr_settings');
      if (saved) {
        const parsed = JSON.parse(saved);
        if (parsed.all_sources_mode) {
          return ALL_SOURCES_DRIVE;
        }
      }
    } catch (e) {}
    return null;
  });
  const [packages, setPackages] = useState([]);
  const [storage, setStorage] = useState(null);
  const [loadingDrives, setLoadingDrives] = useState(true);
  const [loadingPackages, setLoadingPackages] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [sortBy, setSortBy] = useState('date-desc');
  const [selectedTitleId, setSelectedTitleId] = useState(null);
  const [notification, setNotification] = useState(null);
  const [initialStatusLoaded, setInitialStatusLoaded] = useState(false);
  const [batchInstall, setBatchInstall] = useState(() => {
    try {
      const saved = localStorage.getItem('pkg_batch_install');
      return saved ? JSON.parse(saved) : null;
    } catch (e) {
      return null;
    }
  });

  const [settings, setSettings] = useState(() => {
    try {
      const saved = localStorage.getItem('pkgmgr_settings');
      if (saved) return JSON.parse(saved);
    } catch (e) {}
    return {
      move_installed_to_end: true,
      fade_installed_packages: true,
      all_sources_mode: false,
      smb_shares: []
    };
  });
  const [showSettings, setShowSettings] = useState(false);
  const [showSmbPage, setShowSmbPage] = useState(false);
  const [cacheStats, setCacheStats] = useState(null);
  const [loadingStats, setLoadingStats] = useState(false);
  const [showClearCacheModal, setShowClearCacheModal] = useState(false);
  const [clearingCache, setClearingCache] = useState(false);

  const [leftoversData, setLeftoversData] = useState(null);
  const [scanningLeftovers, setScanningLeftovers] = useState(false);
  const [selectedLeftoverToDelete, setSelectedLeftoverToDelete] = useState(null);
  const [deletingLeftover, setDeletingLeftover] = useState(false);
  const [showDonateQr, setShowDonateQr] = useState(false);
  const [showDonateModal, setShowDonateModal] = useState(false);
  const [donateNeverNotice, setDonateNeverNotice] = useState(false);
  const [showModalQr, setShowModalQr] = useState(false);

  const [showSmbModal, setShowSmbModal] = useState(false);
  const [smbEditIndex, setSmbEditIndex] = useState(-1);
  const [smbForm, setSmbForm] = useState({
    id: '',
    label: '',
    server: '',
    port: 445,
    share: '',
    path: '',
    username: '',
    password: '',
    workgroup: 'WORKGROUP',
    is_read_only: false,
    enabled: true
  });
  const [smbTesting, setSmbTesting] = useState(false);
  const [smbTestResult, setSmbTestResult] = useState(null);

  const [scanStatus, setScanStatus] = useState({
    is_scanning: false,
    total_files: 0,
    processed_files: 0,
    current_drive: '',
    current_file: '',
    progress: 0
  });

  const batchInstallRef = useRef(batchInstall);
  useEffect(() => {
    batchInstallRef.current = batchInstall;
  }, [batchInstall]);

  const [installerStatus, setInstallerStatus] = useState({
    is_installing: false,
    pkg_path: '',
    title_id: '',
    title_name: '',
    content_id: '',
    status: 'idle',
    downloaded_bytes: 0,
    total_bytes: 0,
    progress: 0,
    error_code: 0,
    completed: false,
    failed: false,
    is_multipart: false,
    current_part: 0,
    total_parts: 0,
    waiting_for_disc: false,
    prompt_message: ''
  });

  const toastTimeoutRef = useRef(null);
  const selectedDriveRef = useRef(selectedDrive);
  useEffect(() => {
    selectedDriveRef.current = selectedDrive;
  }, [selectedDrive]);
  const selectedTitleIdRef = useRef(selectedTitleId);
  selectedTitleIdRef.current = selectedTitleId;
  useEffect(() => {
    selectedTitleIdRef.current = selectedTitleId;
  }, [selectedTitleId]);
  const installerStatusRef = useRef(installerStatus);
  useEffect(() => {
    installerStatusRef.current = installerStatus;
  }, [installerStatus]);

  // Determine active full-screen overlay state and install flags
  const isWaitingForPart =
    installerStatus.is_installing &&
    (installerStatus.waiting_for_disc || installerStatus.status === 'waiting_disc');
  const isBatchActive = !!(batchInstall && (installerStatus.is_installing || batchInstall.stage === 'update'));
  const isInstalling = (installerStatus.is_installing || isBatchActive) && !isWaitingForPart;

  const wasInstallingRef = useRef(false);
  const scrollPositionRef = useRef(0);
  const shouldRestoreScrollRef = useRef(false);
  const detailScrollPositionRef = useRef(0);
  const shouldRestoreDetailScrollRef = useRef(false);
  const checkOnlineRef = useRef(null);

  // Speed and ETA calculation refs
  const speedCalcRef = useRef({
    lastBytes: 0,
    lastTime: 0,
    speed: 0
  });

  const showToast = (message, type = 'info') => {
    if (toastTimeoutRef.current) clearTimeout(toastTimeoutRef.current);
    setNotification({ message, type });
    toastTimeoutRef.current = setTimeout(() => {
      setNotification(null);
      toastTimeoutRef.current = null;
    }, 4000);
  };

  const fetchStorage = async () => {
    try {
      const res = await fetch('/api/storage');
      if (res.ok) {
        const data = await res.json();
        setStorage(data);
      }
    } catch (err) {
      // ignore
    }
  };

  const fetchDrives = async () => {
    try {
      const res = await fetch('/api/drives');
      if (res.ok) {
        const data = await res.json();
        setDrives(data);
      }
    } catch (err) {
      // ignore
    } finally {
      setLoadingDrives(false);
    }
  };

  const fetchPackagesForDrive = async (drive, silent = false) => {
    const targetDrive = drive || selectedDriveRef.current;
    if (!targetDrive) return;
    if (!silent) setLoadingPackages(true);
    try {
      let url = '/api/packages';
      if (targetDrive.id !== '__all__') {
        const driveQuery = encodeURIComponent(targetDrive.id || targetDrive.path);
        url = `/api/packages?drive=${driveQuery}`;
      }
      const res = await fetch(url);
      if (res.ok) {
        const data = await res.json();
        setPackages(data);
      }
    } catch (err) {
      // ignore
    } finally {
      if (!silent) setLoadingPackages(false);
    }
  };

  const refreshAll = async () => {
    setRefreshing(true);
    setScanStatus({
      is_scanning: true,
      total_files: 0,
      processed_files: 0,
      current_drive: 'Scanning storage media...',
      current_file: '',
      progress: 0
    });

    const pollScanTimer = setInterval(async () => {
      try {
        const res = await fetch('/api/scan/status');
        if (res.ok) {
          const data = await res.json();
          setScanStatus(data);
        }
      } catch (e) {}
    }, 250);

    try {
      const res = await fetch('/api/packages/refresh', { method: 'POST' });
      if (res.ok) {
        await Promise.all([fetchDrives(), fetchStorage()]);
        if (selectedDriveRef.current) {
          await fetchPackagesForDrive(selectedDriveRef.current, true);
        }
        showToast('Refreshed package catalog', 'success');
      } else {
        showToast('Failed to refresh packages', 'error');
      }
    } catch (err) {
      showToast('Error refreshing: ' + err.message, 'error');
    } finally {
      clearInterval(pollScanTimer);
      setScanStatus((prev) => ({ ...prev, is_scanning: false }));
      setTimeout(() => {
        setRefreshing(false);
      }, 400);
    }
  };

  const refreshingRef = useRef(false);
  const scanStatusRef = useRef(scanStatus);
  useEffect(() => { refreshingRef.current = refreshing; }, [refreshing]);
  useEffect(() => { scanStatusRef.current = scanStatus; }, [scanStatus]);

  const quickScanInProgressRef = useRef(false);

  const triggerQuickScan = useCallback(async (drive) => {
    // 1. Never interrupt an active installation or optical disc wait
    if (installerStatusRef.current?.is_installing || installerStatusRef.current?.waiting_for_disc) {
      return;
    }
    // 2. Never collide with a user-initiated full rescan
    if (refreshingRef.current || scanStatusRef.current?.is_scanning) {
      return;
    }
    // 3. Do not overlap quick scans
    if (quickScanInProgressRef.current) {
      return;
    }

    quickScanInProgressRef.current = true;
    try {
      const targetDrive = drive || selectedDriveRef.current;
      let url = '/api/packages/quick-scan';
      if (targetDrive && targetDrive.id && targetDrive.id !== '__all__') {
        url += `?drive=${encodeURIComponent(targetDrive.id)}`;
      }
      const res = await fetch(url, { method: 'POST' });
      if (res.ok) {
        const data = await res.json();
        if (data.changed) {
          // Silently update catalog, drives, and storage without blocking UI or showing overlay
          await Promise.all([
            fetchDrives(),
            fetchStorage(),
            fetchPackagesForDrive(selectedDriveRef.current, true)
          ]);
        }
      }
    } catch (err) {
      // Background quick scan failures are silent
    } finally {
      quickScanInProgressRef.current = false;
    }
  }, []);

  const fetchSettings = async () => {
    try {
      const res = await fetch('/api/settings');
      if (res.ok) {
        const data = await res.json();
        setSettings((prev) => {
          const updated = { ...prev, ...data };
          try { localStorage.setItem('pkgmgr_settings', JSON.stringify(updated)); } catch (e) {}
          return updated;
        });
        if (data.all_sources_mode && !selectedDriveRef.current) {
          setSelectedDrive(ALL_SOURCES_DRIVE);
          selectedDriveRef.current = ALL_SOURCES_DRIVE;
          fetchPackagesForDrive(ALL_SOURCES_DRIVE);
        }
      }
    } catch (err) {}
  };

  const fetchCacheStats = async () => {
    setLoadingStats(true);
    try {
      const res = await fetch('/api/cache/stats');
      if (res.ok) {
        const data = await res.json();
        setCacheStats(data);
      }
    } catch (err) {} finally {
      setLoadingStats(false);
    }
  };

  const handleSaveSettings = async (newSettings) => {
    setSettings(newSettings);
    try {
      localStorage.setItem('pkgmgr_settings', JSON.stringify(newSettings));
    } catch (e) {}

    if (newSettings.all_sources_mode && !selectedDriveRef.current) {
      setSelectedDrive(ALL_SOURCES_DRIVE);
      selectedDriveRef.current = ALL_SOURCES_DRIVE;
      fetchPackagesForDrive(ALL_SOURCES_DRIVE);
    } else if (!newSettings.all_sources_mode && selectedDriveRef.current?.id === '__all__') {
      setSelectedDrive(null);
      selectedDriveRef.current = null;
      setPackages([]);
      fetchDrives();
      fetchStorage();
    }

    try {
      const res = await fetch('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newSettings)
      });
      if (res.ok) {
        showToast('Settings saved', 'success');
      }
    } catch (e) {
      showToast('Failed to save settings: ' + e.message, 'error');
    }
  };

  const handleClearCache = async () => {
    setClearingCache(true);
    try {
      const res = await fetch('/api/cache/clear', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({})
      });
      const data = await res.json();
      if (res.ok && data.success) {
        showToast(`Cache cleared! Freed ${formatBytes(data.freed_bytes)}.`, 'success');
        setShowClearCacheModal(false);
        fetchCacheStats();
      } else {
        showToast(data.error || 'Failed to clear cache', 'error');
      }
    } catch (err) {
      showToast('Clear cache failed: ' + err.message, 'error');
    } finally {
      setClearingCache(false);
    }
  };

  const [installingShortcut, setInstallingShortcut] = useState(false);

  const handleInstallShortcut = async () => {
    setInstallingShortcut(true);
    try {
      const res = await fetch('/api/shortcut/install', { method: 'POST' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      if (data.success) {
        showToast('PKG Manager shortcut installed to Media tab!', 'success');
      } else {
        showToast('Failed to install home screen shortcut.', 'error');
      }
    } catch (e) {
      showToast('Shortcut install error: ' + e.message, 'error');
    } finally {
      setInstallingShortcut(false);
    }
  };

  const handleScanLeftovers = async () => {
    setScanningLeftovers(true);
    try {
      const res = await fetch('/api/leftovers');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setLeftoversData(data);
      if (data.count === 0) {
        showToast('Scan complete: No orphaned leftovers found.', 'info');
      } else {
        showToast(`Found ${data.count} orphaned package leftover(s).`, 'info');
      }
    } catch (e) {
      showToast('Failed to scan for leftovers: ' + e.message, 'error');
    } finally {
      setScanningLeftovers(false);
    }
  };

  const handleConfirmDeleteLeftover = async (item) => {
    if (!item || !item.title_id) return;
    setDeletingLeftover(true);
    try {
      const res = await fetch('/api/leftovers/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ title_id: item.title_id })
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      if (data.success) {
        showToast(`Deleted leftovers for ${item.title_name || item.title_id} (freed ${formatBytes(data.freed_bytes || 0)})`, 'success');
        setSelectedLeftoverToDelete(null);
        // Refresh leftovers list
        await handleScanLeftovers();
        // Refresh storage info
        fetchStorage();
        // Trigger rescan of drive packages if on package list
        if (selectedDriveRef.current) {
          fetchPackagesForDrive(selectedDriveRef.current);
        }
      } else {
        showToast(`Failed to delete leftover: ${data.error || 'Unknown error'}`, 'error');
      }
    } catch (e) {
      showToast('Error deleting leftover: ' + e.message, 'error');
    } finally {
      setDeletingLeftover(false);
    }
  };

  const handleOpenLeftoverCleanupForTitle = async (titleId, titleName) => {
    if (!titleId) return;
    try {
      showToast('Checking leftover paths...', 'info');
      const res = await fetch('/api/leftovers');
      if (res.ok) {
        const data = await res.json();
        const match = Array.isArray(data.leftovers)
          ? data.leftovers.find((l) => l.title_id && l.title_id.toUpperCase() === titleId.toUpperCase())
          : null;
        if (match) {
          setSelectedLeftoverToDelete(match);
          return;
        }
      }
    } catch (e) {
      // Fallback below
    }

    setSelectedLeftoverToDelete({
      title_id: titleId,
      title_name: titleName || titleId,
      total_size: 0,
      paths: [
        `/user/patch/${titleId}`,
        `/user/patch0/${titleId}`,
        `/user/addcont/${titleId}`,
        `/system_data/priv/appmeta/${titleId}`,
        `/user/appmeta/${titleId}`
      ]
    });
  };

  const handleSaveSmbShare = async () => {
    const rawServer = (smbForm.server || '').trim();
    const rawShare = (smbForm.share || '').trim();
    const rawPath = (smbForm.path || '').trim();

    let cleanServer = rawServer.replace(/^smb:\/\/+/i, '').replace(/^[\\/]+|[\\/]+$/g, '');
    let cleanShare = rawShare.replace(/^[\\/]+|[\\/]+$/g, '');
    let cleanPath = rawPath.replace(/^[\\/]+|[\\/]+$/g, '').replace(/\\/g, '/');

    if (cleanServer.includes('/') || cleanServer.includes('\\')) {
      const parts = cleanServer.split(/[\\/]+/).filter(Boolean);
      cleanServer = parts[0] || '';
      if (!cleanShare && parts.length > 1) {
        cleanShare = parts[1];
      }
      if (parts.length > 2) {
        const subpath = parts.slice(2).join('/');
        cleanPath = cleanPath ? `${subpath}/${cleanPath}` : subpath;
      }
    }

    let port = parseInt(smbForm.port, 10) || 445;
    const firstColon = cleanServer.indexOf(':');
    const lastColon = cleanServer.lastIndexOf(':');
    let portColon = -1;

    if (firstColon > 0 && firstColon === lastColon) {
      portColon = firstColon;
    } else if (firstColon > 0 && firstColon !== lastColon) {
      const bracketIdx = cleanServer.lastIndexOf(']');
      if (bracketIdx > 0 && lastColon > bracketIdx) {
        portColon = lastColon;
      }
    }

    if (portColon > 0) {
      const portCandidate = cleanServer.slice(portColon + 1);
      if (/^\d+$/.test(portCandidate)) {
        port = parseInt(portCandidate, 10);
        cleanServer = cleanServer.slice(0, portColon);
      }
    }

    if (!cleanServer || !cleanShare) {
      showToast('Server and Share name are required', 'error');
      return;
    }
    const currentShares = Array.isArray(settings.smb_shares) ? [...settings.smb_shares] : [];
    const formCopy = {
      ...smbForm,
      server: cleanServer,
      port,
      share: cleanShare,
      path: cleanPath,
      username: (smbForm.username || '').trim(),
      workgroup: (smbForm.workgroup || '').trim() || 'WORKGROUP'
    };
    if (!formCopy.label.trim()) {
      formCopy.label = `${cleanServer}/${cleanShare}`;
    }
    if (!formCopy.id) {
      formCopy.id = 'smb_' + Date.now().toString(36);
    }
    if (smbEditIndex >= 0 && smbEditIndex < currentShares.length) {
      currentShares[smbEditIndex] = formCopy;
    } else {
      currentShares.push(formCopy);
    }
    const newSettings = { ...settings, smb_shares: currentShares };
    await handleSaveSettings(newSettings);
    setShowSmbModal(false);
    setSmbTestResult(null);
    refreshAll();
  };

  const handleRemoveSmbShare = async (idx) => {
    const currentShares = Array.isArray(settings.smb_shares) ? [...settings.smb_shares] : [];
    currentShares.splice(idx, 1);
    const newSettings = { ...settings, smb_shares: currentShares };
    await handleSaveSettings(newSettings);
    refreshAll();
  };

  const handleToggleSmbShare = async (idx) => {
    const currentShares = Array.isArray(settings.smb_shares) ? [...settings.smb_shares] : [];
    if (currentShares[idx]) {
      currentShares[idx] = { ...currentShares[idx], enabled: !currentShares[idx].enabled };
      const newSettings = { ...settings, smb_shares: currentShares };
      await handleSaveSettings(newSettings);
      refreshAll();
    }
  };

  const handleTestSmbConnection = async (shareCfg) => {
    if (!shareCfg) {
      showToast('No share configuration provided', 'error');
      return;
    }

    const rawServer = (shareCfg.server || '').trim();
    const rawShare = (shareCfg.share || '').trim();
    const rawPath = (shareCfg.path || '').trim();

    let cleanServer = rawServer.replace(/^smb:\/\/+/i, '').replace(/^[\\/]+|[\\/]+$/g, '');
    let cleanShare = rawShare.replace(/^[\\/]+|[\\/]+$/g, '');
    let cleanPath = rawPath.replace(/^[\\/]+|[\\/]+$/g, '').replace(/\\/g, '/');

    if (cleanServer.includes('/') || cleanServer.includes('\\')) {
      const parts = cleanServer.split(/[\\/]+/).filter(Boolean);
      cleanServer = parts[0] || '';
      if (!cleanShare && parts.length > 1) {
        cleanShare = parts[1];
      }
      if (parts.length > 2) {
        const subpath = parts.slice(2).join('/');
        cleanPath = cleanPath ? `${subpath}/${cleanPath}` : subpath;
      }
    }

    let port = parseInt(shareCfg.port, 10) || 445;
    const firstColon = cleanServer.indexOf(':');
    const lastColon = cleanServer.lastIndexOf(':');
    let portColon = -1;

    if (firstColon > 0 && firstColon === lastColon) {
      portColon = firstColon;
    } else if (firstColon > 0 && firstColon !== lastColon) {
      const bracketIdx = cleanServer.lastIndexOf(']');
      if (bracketIdx > 0 && lastColon > bracketIdx) {
        portColon = lastColon;
      }
    }

    if (portColon > 0) {
      const portCandidate = cleanServer.slice(portColon + 1);
      if (/^\d+$/.test(portCandidate)) {
        port = parseInt(portCandidate, 10);
        cleanServer = cleanServer.slice(0, portColon);
      }
    }

    if (!cleanServer || !cleanShare) {
      showToast('Server and Share name are required', 'error');
      return;
    }

    setSmbTesting(true);
    setSmbTestResult(null);

    const sanitizedCfg = {
      ...shareCfg,
      server: cleanServer,
      port,
      share: cleanShare,
      path: cleanPath,
      username: (shareCfg.username || '').trim(),
      workgroup: (shareCfg.workgroup || '').trim() || 'WORKGROUP'
    };

    try {
      const res = await fetch('/api/smb/test', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(sanitizedCfg)
      });

      const text = await res.text();
      let data = null;
      try {
        data = text ? JSON.parse(text) : null;
      } catch {
        data = null;
      }

      if (data && typeof data === 'object') {
        setSmbTestResult(data);
        if (data.success) {
          showToast(data.message || 'Connection successful', 'success');
        } else {
          showToast(data.message || data.error || 'Connection failed', 'error');
        }
      } else if (res.ok) {
        const fallbackMsg = text ? text.slice(0, 120) : 'Connection test returned unexpected response';
        setSmbTestResult({ success: false, message: fallbackMsg });
        showToast(fallbackMsg, 'error');
      } else {
        const errorMsg = text ? `Server error (${res.status}): ${text.slice(0, 120)}` : `Server error: HTTP ${res.status}`;
        setSmbTestResult({ success: false, message: errorMsg });
        showToast('Connection failed: HTTP ' + res.status, 'error');
      }
    } catch (e) {
      const errMsg = e?.message || String(e) || 'Unknown error';
      setSmbTestResult({ success: false, message: errMsg });
      showToast('Connection test error: ' + errMsg, 'error');
    } finally {
      setSmbTesting(false);
    }
  };

  const fetchStatus = async () => {
    try {
      const res = await fetch('/api/poll');
      if (res.ok) {
        const data = await res.json();
        // If an install just started while on package details page, ensure detail scroll position is preserved
        if (data.is_installing && !installerStatusRef.current?.is_installing && selectedTitleIdRef.current) {
          if (!shouldRestoreDetailScrollRef.current) {
            const currentY = window.scrollY || window.pageYOffset || (document.documentElement && document.documentElement.scrollTop) || (document.body && document.body.scrollTop) || 0;
            detailScrollPositionRef.current = currentY;
            shouldRestoreDetailScrollRef.current = true;
          }
        }
        setInstallerStatus(data);
        setInitialStatusLoaded(true);

        // Track speed for ETA calculation
        const now = Date.now();
        if (data.is_installing && data.downloaded_bytes > 0) {
          if (speedCalcRef.current.lastTime > 0) {
            const timeDiffSec = (now - speedCalcRef.current.lastTime) / 1000;
            if (timeDiffSec >= 1.0) {
              const bytesDiff = data.downloaded_bytes - speedCalcRef.current.lastBytes;
              if (bytesDiff >= 0) {
                const instantSpeed = bytesDiff / timeDiffSec;
                speedCalcRef.current.speed = speedCalcRef.current.speed === 0
                  ? instantSpeed
                  : (speedCalcRef.current.speed * 0.7 + instantSpeed * 0.3);
              }
              speedCalcRef.current.lastBytes = data.downloaded_bytes;
              speedCalcRef.current.lastTime = now;
            }
          } else {
            speedCalcRef.current.lastBytes = data.downloaded_bytes;
            speedCalcRef.current.lastTime = now;
          }
        } else {
          speedCalcRef.current.lastBytes = 0;
          speedCalcRef.current.lastTime = 0;
          speedCalcRef.current.speed = 0;
        }

        // An install ended: update catalog and storage
        if (wasInstallingRef.current && !data.is_installing && (data.completed || data.failed)) {
          fetchStorage();
          if (selectedDriveRef.current) {
            fetchPackagesForDrive(selectedDriveRef.current);
          }
        }

        // Batch install sequential queue handling:
        const currentBatch = batchInstallRef.current;
        if (currentBatch) {
          if (currentBatch.stage === 'base' && wasInstallingRef.current && !data.is_installing && data.completed) {
            // Stage 1 (Base) complete! Transition to Stage 2 (Update)
            const nextBatch = { ...currentBatch, stage: 'update' };
            try {
              localStorage.setItem('pkg_batch_install', JSON.stringify(nextBatch));
            } catch (e) {}
            setBatchInstall(nextBatch);
            showToast(`Base installed! Installing update for ${currentBatch.titleName}...`, 'info');

            fetch('/api/install', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ path: currentBatch.updatePkg.path })
            }).then(async (r) => {
              const resJson = await r.json();
              if (!r.ok || !resJson.success) {
                showToast(resJson.error || 'Failed to start update installation', 'error');
                try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
                setBatchInstall(null);
              } else {
                fetchStatus();
              }
            }).catch((e) => {
              showToast('Update install request failed: ' + e.message, 'error');
              try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
              setBatchInstall(null);
            });
          } else if (currentBatch.stage === 'update' && wasInstallingRef.current && !data.is_installing && (data.completed || data.failed)) {
            // Stage 2 (Update) complete!
            try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
            setBatchInstall(null);
            if (data.completed) {
              showToast(`Base + Update installed for ${currentBatch.titleName}!`, 'success');
            }
          } else if (data.failed && !data.is_installing) {
            try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
            setBatchInstall(null);
          }
        }

        wasInstallingRef.current = data.is_installing;
      }
    } catch (err) {
      // Backend may be momentarily busy
    }
  };

  const handleSelectDrive = (drive) => {
    if (!drive.clickable) return;
    setSelectedDrive(drive);
    selectedDriveRef.current = drive;
    setSelectedTitleId(null);
    selectedTitleIdRef.current = null;
    scrollPositionRef.current = 0;
    detailScrollPositionRef.current = 0;
    shouldRestoreDetailScrollRef.current = false;
    setSearchQuery('');
    fetchPackagesForDrive(drive);
    triggerQuickScan(drive);
    window.scrollTo(0, 0);
  };

  const handleBackToDrives = () => {
    setSelectedDrive(null);
    selectedDriveRef.current = null;
    setSelectedTitleId(null);
    selectedTitleIdRef.current = null;
    scrollPositionRef.current = 0;
    detailScrollPositionRef.current = 0;
    shouldRestoreDetailScrollRef.current = false;
    setPackages([]);
    setSearchQuery('');
    fetchDrives();
    fetchStorage();
    window.scrollTo(0, 0);
  };

  const handleOpenTitle = (titleId) => {
    const currentY = window.scrollY || window.pageYOffset || (document.documentElement && document.documentElement.scrollTop) || (document.body && document.body.scrollTop) || 0;
    scrollPositionRef.current = currentY;
    detailScrollPositionRef.current = 0;
    shouldRestoreDetailScrollRef.current = false;
    setSelectedTitleId(titleId);
    selectedTitleIdRef.current = titleId;
    window.scrollTo(0, 0);
  };

  const handleBackToPackages = () => {
    shouldRestoreScrollRef.current = true;
    detailScrollPositionRef.current = 0;
    shouldRestoreDetailScrollRef.current = false;
    setSelectedTitleId(null);
    selectedTitleIdRef.current = null;
  };

  const handleInstall = async (pkg) => {
    if (installerStatus.is_installing) {
      showToast('Another installation is already in progress', 'warning');
      return;
    }

    try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
    setBatchInstall(null);

    const requiredSpace = Number(pkg.total_pkg_size || pkg.file_size) || 0;
    if (storage && storage.free && storage.free < requiredSpace) {
      showToast(
        `Insufficient storage! Needs ${formatBytes(requiredSpace)}, but only ${formatBytes(storage.free)} is available.`,
        'error'
      );
      return;
    }

    if (selectedTitleId) {
      const currentY = window.scrollY || window.pageYOffset || (document.documentElement && document.documentElement.scrollTop) || (document.body && document.body.scrollTop) || 0;
      detailScrollPositionRef.current = currentY;
      shouldRestoreDetailScrollRef.current = true;
    }

    speedCalcRef.current = { lastBytes: 0, lastTime: 0, speed: 0 };
    showToast(`Starting installation for ${pkg.title_name || 'package'}...`, 'info');

    try {
      const res = await fetch('/api/install', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: pkg.path })
      });

      const data = await res.json();
      if (!res.ok || !data.success) {
        showToast(data.error || 'Failed to start installation', 'error');
        shouldRestoreDetailScrollRef.current = false;
      } else {
        showToast(`Installing ${pkg.title_name || 'package'}...`, 'success');
        fetchStatus();
      }
    } catch (err) {
      showToast('Install request failed: ' + err.message, 'error');
      shouldRestoreDetailScrollRef.current = false;
    }
  };

  const handleInstallBaseAndUpdate = async (basePkg, updatePkg) => {
    if (installerStatus.is_installing) {
      showToast('Another installation is already in progress', 'warning');
      return;
    }

    const baseRequired = Number(basePkg.total_pkg_size || basePkg.file_size) || 0;
    const updateRequired = Number(updatePkg.total_pkg_size || updatePkg.file_size) || 0;
    const combinedSpace = baseRequired + updateRequired;
    if (storage && storage.free && storage.free < combinedSpace) {
      showToast(
        `Insufficient storage! Needs ${formatBytes(combinedSpace)}, but only ${formatBytes(storage.free)} is available.`,
        'error'
      );
      return;
    }

    if (selectedTitleId) {
      const currentY = window.scrollY || window.pageYOffset || (document.documentElement && document.documentElement.scrollTop) || (document.body && document.body.scrollTop) || 0;
      detailScrollPositionRef.current = currentY;
      shouldRestoreDetailScrollRef.current = true;
    }

    const batch = {
      stage: 'base',
      basePkg,
      updatePkg,
      baseSize: baseRequired,
      updateSize: updateRequired,
      combinedTotal: combinedSpace,
      titleName: selectedTitle?.title_name || basePkg.title_name || 'Title',
      titleId: selectedTitle?.title_id || basePkg.title_id || '',
      iconPath: selectedTitle?.iconPath || basePkg.path
    };

    try {
      localStorage.setItem('pkg_batch_install', JSON.stringify(batch));
    } catch (e) {}
    setBatchInstall(batch);

    speedCalcRef.current = { lastBytes: 0, lastTime: 0, speed: 0 };
    showToast(`Starting Base + Update install for ${batch.titleName}...`, 'info');

    try {
      const res = await fetch('/api/install', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: basePkg.path })
      });

      const data = await res.json();
      if (!res.ok || !data.success) {
        showToast(data.error || 'Failed to start base installation', 'error');
        try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
        setBatchInstall(null);
        shouldRestoreDetailScrollRef.current = false;
      } else {
        fetchStatus();
      }
    } catch (err) {
      showToast('Install request failed: ' + err.message, 'error');
      try { localStorage.removeItem('pkg_batch_install'); } catch (e) {}
      setBatchInstall(null);
      shouldRestoreDetailScrollRef.current = false;
    }
  };

  const handleCancel = async () => {
    try {
      localStorage.removeItem('pkg_batch_install');
    } catch (e) {}
    setBatchInstall(null);

    try {
      const res = await fetch('/api/cancel', { method: 'POST' });
      const data = await res.json();
      if (res.ok && data.success) {
        showToast('Installation canceled', 'info');
        fetchStatus();
      } else {
        showToast(data.error || 'Failed to cancel installation', 'error');
      }
    } catch (err) {
      showToast('Cancel request failed: ' + err.message, 'error');
    }
  };

  useEffect(() => {
    document.title = getBrowserTitle();
    let unmounted = false;
    let retryTimer = null;

    const checkOnline = async () => {
      let offline = false;
      try {
        const res = await fetch('/api/version');
        if (!res.ok) {
          offline = true;
        } else {
          const verRes = await res.text();
          if (verRes.toLowerCase().includes('<!doctype') || !verRes.trim()) {
            offline = true;
          } else {
            const v = verRes.trim();
            if (!unmounted) {
              setAppVersion(v);
              setIsOffline(false);
            }
            document.title = getBrowserTitle(v);
          }
        }
      } catch (e) {
        offline = true;
      }

      if (offline) {
        if (!unmounted) setIsOffline(true);
        retryTimer = setTimeout(checkOnline, 4000);
        return;
      }

      fetchDrives();
      fetchStorage();
      fetchStatus();
      fetchSettings();
      if (selectedDriveRef.current) {
        fetchPackagesForDrive(selectedDriveRef.current);
      }
      triggerQuickScan(selectedDriveRef.current);
    };

    checkOnlineRef.current = checkOnline;
    checkOnline();
    return () => {
      unmounted = true;
      if (retryTimer) clearTimeout(retryTimer);
    };
  }, []);

  useEffect(() => {
    if (!selectedTitleId && shouldRestoreScrollRef.current) {
      shouldRestoreScrollRef.current = false;
      const targetY = scrollPositionRef.current || 0;
      const restore = () => {
        window.scrollTo(0, targetY);
        if (document.documentElement) document.documentElement.scrollTop = targetY;
        if (document.body) document.body.scrollTop = targetY;
      };
      restore();
      requestAnimationFrame(() => {
        restore();
        setTimeout(restore, 20);
        setTimeout(restore, 80);
      });
    }
  }, [selectedTitleId]);

  useEffect(() => {
    if (isOffline) return;
    // Poll status faster (1s) when active install or waiting for disc, standard (3s) when idle
    const intervalTime = (installerStatus.is_installing || installerStatus.waiting_for_disc) ? 1000 : 3000;
    const interval = setInterval(() => {
      fetchStatus();
    }, intervalTime);
    return () => clearInterval(interval);
  }, [installerStatus.is_installing, installerStatus.waiting_for_disc, isOffline]);

  useEffect(() => {
    if (isOffline) return;
    // Periodically run a light/quick scan every ~15s when on a specific drive page
    // (or in all-sources mode) to catch new/modified files without user intervention
    const interval = setInterval(() => {
      const curDrive = selectedDriveRef.current;
      if (curDrive && curDrive.id) {
        triggerQuickScan(curDrive);
      }
    }, 15000);
    return () => clearInterval(interval);
  }, [triggerQuickScan, isOffline]);

  // Trigger 7-day donation popup modal (only after the first week of use)
  useEffect(() => {
    if (!initialStatusLoaded) return;
    if (installerStatus?.is_installing || isWaitingForPart) return;

    try {
      const raw = localStorage.getItem(DONATE_MODAL_STORAGE_KEY);
      if (!raw) {
        // First run: record initial timestamp so it won't show for the first week
        localStorage.setItem(DONATE_MODAL_STORAGE_KEY, JSON.stringify({ never: false, lastShown: Date.now() }));
        return;
      }
      const data = JSON.parse(raw);
      if (data && data.never) return;
      if (!data || !data.lastShown) {
        localStorage.setItem(DONATE_MODAL_STORAGE_KEY, JSON.stringify({ never: false, lastShown: Date.now() }));
        return;
      }
      if ((Date.now() - Number(data.lastShown)) < DONATE_MODAL_INTERVAL_MS) {
        return;
      }
      const timer = setTimeout(() => {
        setShowDonateModal(true);
      }, 1200);
      return () => clearTimeout(timer);
    } catch (e) {}
  }, [initialStatusLoaded, installerStatus?.is_installing, isWaitingForPart]);

  const handleCloseDonateModal = useCallback(() => {
    try {
      localStorage.setItem(DONATE_MODAL_STORAGE_KEY, JSON.stringify({ never: false, lastShown: Date.now() }));
    } catch (e) {}
    setShowDonateModal(false);
  }, []);

  const handleNeverShowDonateModal = useCallback(() => {
    try {
      localStorage.setItem(DONATE_MODAL_STORAGE_KEY, JSON.stringify({ never: true, lastShown: Date.now() }));
    } catch (e) {}
    setDonateNeverNotice(true);
    setTimeout(() => {
      setShowDonateModal(false);
      setDonateNeverNotice(false);
    }, 2000);
  }, []);

  // Calculate ETA string & speed
  const etaInfo = useMemo(() => {
    if (!installerStatus.is_installing) return null;
    const isBatch = !!(batchInstall && batchInstall.combinedTotal > 0);
    const total = isBatch ? batchInstall.combinedTotal : installerStatus.total_bytes;
    if (total <= 0) return null;

    let downloaded = installerStatus.downloaded_bytes;
    if (isBatch) {
      if (batchInstall.stage === 'base') {
        downloaded = Math.min(batchInstall.baseSize, installerStatus.downloaded_bytes);
      } else {
        downloaded = batchInstall.baseSize + Math.min(batchInstall.updateSize, installerStatus.downloaded_bytes);
      }
    }

    const remaining = Math.max(0, total - downloaded);
    const speed = speedCalcRef.current.speed;
    if (!speed || speed < 1024) {
      return { text: 'Calculating ETA...', speedStr: '' };
    }
    const seconds = remaining / speed;
    return {
      text: formatEta(seconds) || 'Calculating ETA...',
      speedStr: `${formatBytes(speed)}/s`
    };
  }, [installerStatus.is_installing, installerStatus.downloaded_bytes, installerStatus.total_bytes, batchInstall]);

  // Group packages by title_id
  const groupedTitles = useMemo(() => {
    const isAllSources = selectedDrive?.id === '__all__';
    const filtered = packages.filter((p) => {
      if (p.filename && p.filename.startsWith('.')) return false;
      if (!searchQuery.trim()) return true;
      const q = searchQuery.toLowerCase();
      return (
        (p.title_name && p.title_name.toLowerCase().indexOf(q) !== -1) ||
        (p.title_id && p.title_id.toLowerCase().indexOf(q) !== -1) ||
        (p.content_id && p.content_id.toLowerCase().indexOf(q) !== -1) ||
        (p.app_version && p.app_version.toLowerCase().indexOf(q) !== -1) ||
        (p.pkg_type && p.pkg_type.toLowerCase().indexOf(q) !== -1)
      );
    });

    const groupMap = new Map();
    for (const pkg of filtered) {
      const baseKey = (pkg.title_id && pkg.title_id.trim() && pkg.title_id.trim().toUpperCase() !== 'UNKNOWN')
        ? pkg.title_id.trim().toUpperCase()
        : (pkg.title_name && pkg.title_name !== 'Unknown Package' && pkg.title_name !== 'Package' ? pkg.title_name : pkg.filename || pkg.path || 'unknown');

      const srcInfo = getSourceInfo(pkg.path, drives);
      const key = isAllSources ? `${baseKey}__${srcInfo.id}` : baseKey;

      if (!groupMap.has(key)) {
        groupMap.set(key, { items: [], srcInfo });
      }
      groupMap.get(key).items.push(pkg);
    }

    const groups = [];
    for (const [key, { items, srcInfo }] of groupMap.entries()) {
      // Find base package
      const base = items.find((p) => p.pkg_type === 'base') || null;

      // Find update packages, sorted newest version first
      const updates = items.filter((p) => p.pkg_type === 'update');
      updates.sort((a, b) => {
        const verA = (a.app_version || '').replace(/^v/, '');
        const verB = (b.app_version || '').replace(/^v/, '');
        return verB.localeCompare(verA, undefined, { numeric: true, sensitivity: 'base' });
      });

      // Find DLC packages
      const dlcs = items.filter((p) => p.pkg_type === 'dlc');

      // Find other/unknown packages
      const others = items.filter(
        (p) => p.pkg_type !== 'base' && p.pkg_type !== 'update' && p.pkg_type !== 'dlc'
      );

      // Representative package:
      // "and use image and title from base if it exists, or update, or first detected dlc"
      const primaryPkg = base || updates[0] || dlcs[0] || others[0] || items[0];

      // Image package (primaryPkg if it has icon, or first item that has icon)
      const imagePkg = (primaryPkg && primaryPkg.has_icon)
        ? primaryPkg
        : items.find((p) => p.has_icon) || primaryPkg;

      const nonGenericTitle = [base, updates[0], dlcs[0], ...items]
        .map((p) => p?.title_name)
        .find((t) => t && t !== 'Package' && t !== 'Unknown Package');
      const titleName = nonGenericTitle || primaryPkg.title_name || primaryPkg.filename || 'Unknown Package';
      const titleId = primaryPkg.title_id || (base && base.title_id) || '';

      // Latest detected update version formatted cleanly
      let latestUpdateVer = null;
      if (updates.length > 0 && updates[0].app_version) {
        latestUpdateVer = formatVersion(updates[0].app_version);
      }

      // Multi-part package tracking and sizes
      const isBaseMultipart = base ? !!base.is_multipart && (Number(base.total_parts) > 1) : false;
      const partIndex = base?.part_index || 1;
      const totalParts = base?.total_parts || 1;
      const firstPartSize = base ? (Number(base.file_size) || 0) : 0;
      const baseFullSize = base ? (Number(base.total_pkg_size || base.file_size) || 0) : 0;

      // Check if ANY package in this group is multi-part
      const hasMultipart = items.some((p) => p.is_multipart && (Number(p.total_parts) > 1));
      const totalDriveSize = items.reduce((sum, p) => sum + (Number(p.file_size) || 0), 0);
      const totalFullSize = items.reduce((sum, p) => sum + (Number(p.total_pkg_size || p.file_size) || 0), 0);

      // Latest mtime across all items in group
      // "the main screen sorting by date should now sort by the date of last item per-package changed"
      const latestMtime = Math.max(...items.map((p) => Number(p.mtime) || 0));
      const totalSize = totalFullSize;

      const isBaseInstalled = base ? base.is_installed : items.some((p) => p.is_installed);
      const rawInstalledVer = base
        ? base.installed_version
        : (items.find((p) => p.installed_version)?.installed_version || '');
      const installedVersion = formatVersion(rawInstalledVer);

      const hasLeftover = base ? !!base.has_leftover : items.some((p) => p.has_leftover);
      const leftoverDesc = (base && base.leftover_desc) || items.find((p) => p.leftover_desc)?.leftover_desc || '';

      const isPartiallyInstalled = base ? !!base.is_partially_installed : items.some((p) => p.is_partially_installed);
      const partialDesc = (base && base.partial_desc) || items.find((p) => p.partial_desc)?.partial_desc || '';

      const hasBaseOnDrive = !!base;
      const hasNewBase = hasBaseOnDrive && (!isBaseInstalled || base?.can_install !== false);

      const isLatestUpdateInstalled = updates.length > 0 && isBaseInstalled && (
        updates[0].can_install === false &&
        updates[0].install_disabled_reason === 'Installed version is same or newer'
      );
      const hasNewUpdate = isBaseInstalled && updates.length > 0 && !isLatestUpdateInstalled;

      const uninstalledDlcs = dlcs.filter((d) => !d.is_dlc_installed && d.install_disabled_reason !== 'DLC is already installed');
      const areAllDlcsInstalled = dlcs.length > 0 && isBaseInstalled && uninstalledDlcs.length === 0;
      const hasNewDlc = isBaseInstalled && dlcs.length > 0 && uninstalledDlcs.length > 0;

      const isBaseUpToDate = isBaseInstalled && (!base || base.can_install === false);
      const isEverythingInstalled = isBaseUpToDate &&
        (updates.length === 0 || isLatestUpdateInstalled) &&
        (dlcs.length === 0 || areAllDlcsInstalled);

      groups.push({
        id: key,
        title_id: titleId,
        title_name: titleName,
        sourceName: srcInfo.name,
        sourceType: srcInfo.type,
        sourceId: srcInfo.id,
        primaryPkg,
        imagePkg,
        has_icon: !!(imagePkg && imagePkg.has_icon),
        iconPath: imagePkg ? imagePkg.path : '',
        base,
        updates,
        dlcs,
        others,
        items,
        latestUpdateVersion: latestUpdateVer,
        dlcCount: dlcs.length,
        latestMtime,
        totalSize,
        totalDriveSize,
        totalFullSize,
        hasMultipart,
        isMultipart: isBaseMultipart,
        isBaseMultipart,
        partIndex,
        totalParts,
        firstPartSize,
        baseFullSize,
        isBaseInstalled,
        installedVersion,
        hasLeftover,
        leftoverDesc,
        isPartiallyInstalled,
        partialDesc,
        hasBaseOnDrive,
        hasNewBase,
        isLatestUpdateInstalled,
        hasNewUpdate,
        uninstalledDlcs,
        areAllDlcsInstalled,
        hasNewDlc,
        isEverythingInstalled
      });
    }

    return groups.sort((a, b) => {
      if (settings.move_installed_to_end) {
        if (a.isEverythingInstalled !== b.isEverythingInstalled) {
          return a.isEverythingInstalled ? 1 : -1;
        }
      }

      const nameA = a.title_name.toLowerCase();
      const nameB = b.title_name.toLowerCase();

      if (sortBy === 'name-asc') {
        return nameA.localeCompare(nameB);
      }
      if (sortBy === 'name-desc') {
        return nameB.localeCompare(nameA);
      }
      if (sortBy === 'date-desc') {
        if (b.latestMtime !== a.latestMtime) return b.latestMtime - a.latestMtime;
        return nameA.localeCompare(nameB);
      }
      if (sortBy === 'date-asc') {
        if (a.latestMtime !== b.latestMtime) return a.latestMtime - b.latestMtime;
        return nameA.localeCompare(nameB);
      }
      if (sortBy === 'size-desc') {
        if (b.totalSize !== a.totalSize) return b.totalSize - a.totalSize;
        return nameA.localeCompare(nameB);
      }
      if (sortBy === 'size-asc') {
        if (a.totalSize !== b.totalSize) return a.totalSize - b.totalSize;
        return nameA.localeCompare(nameB);
      }
      return 0;
    });
  }, [packages, searchQuery, sortBy, settings.move_installed_to_end, selectedDrive, drives]);

  // Selected title for detail view
  const selectedTitle = useMemo(() => {
    if (!selectedTitleId) return null;
    return groupedTitles.find((g) => g.id === selectedTitleId) || null;
  }, [groupedTitles, selectedTitleId]);

  // Restore package details scroll position when returning from installation
  useEffect(() => {
    if (!isInstalling && !isWaitingForPart && !batchInstall && selectedTitleId && shouldRestoreDetailScrollRef.current) {
      shouldRestoreDetailScrollRef.current = false;
      const targetY = detailScrollPositionRef.current || 0;
      const restore = () => {
        window.scrollTo(0, targetY);
        if (document.documentElement) document.documentElement.scrollTop = targetY;
        if (document.body) document.body.scrollTop = targetY;
      };
      restore();
      requestAnimationFrame(() => {
        restore();
        setTimeout(restore, 20);
        setTimeout(restore, 80);
      });
    }
  }, [isInstalling, isWaitingForPart, batchInstall, selectedTitleId]);

  // Source path check: disc vs USB
  const isDiscSource =
    (installerStatus.pkg_path || '').startsWith('/mnt/disc') ||
    (installerStatus.pkg_path || '').indexOf('/disc') !== -1 ||
    (installerStatus.pkg_path || '').indexOf('_disc') !== -1;

  if (isOffline) {
    return (
      <div className="min-h-screen bg-[#0a0a0f] text-zinc-100 font-ps5 flex flex-col items-center justify-center p-4 text-center select-none">
        <div className="max-w-lg p-12 bg-black/40 rounded-3xl border border-white/5 backdrop-blur-sm shadow-2xl">
          <div className="text-7xl font-light text-zinc-400 mb-8 font-mono">:(</div>
          <h1 className="text-2xl font-bold mb-4 text-zinc-200">PKG Manager is not running...</h1>
          <p className="text-lg text-zinc-400 leading-relaxed mb-6">
            Please ensure you have loaded <strong className="text-white font-mono">pkgmgr.elf</strong> on your PS5 before launching this application.
          </p>
          <button
            onClick={() => {
              if (checkOnlineRef.current) checkOnlineRef.current();
              else window.location.reload();
            }}
            className="px-6 py-2.5 bg-[#0070d1] hover:bg-[#0095ff] text-white font-medium rounded-lg transition-colors cursor-pointer"
          >
            Retry Connection
          </button>
        </div>
      </div>
    );
  }

  // Initial loading splash to avoid any flash of buttons if reopening during install
  if (!initialStatusLoaded) {
    return (
      <div className="min-h-screen bg-[#0a0a0f] text-white flex items-center justify-center">
        <div className="text-center">
          <div className="ps5-robust-spinner mx-auto" />
          <p className="text-xs text-zinc-400 mt-3 font-mono">Connecting to PKG Manager...</p>
        </div>
      </div>
    );
  }

  // ──────────────────────────────────────────────────────────────────────────
  // VIEW A: Full-Screen Waiting For Disc / USB Part Overlay
  // Completely hides background to prevent gamepad focus on elements underneath
  // ──────────────────────────────────────────────────────────────────────────
  if (isWaitingForPart) {
    const discTitle = isDiscSource
      ? (installerStatus.prompt_message || `Please Insert Disc ${installerStatus.current_part}`)
      : (installerStatus.prompt_message || `Waiting for Part ${installerStatus.current_part}`);

    const discDesc = isDiscSource
      ? `Disc ${installerStatus.current_part - 1} was copied. Please eject the disc, insert Disc ${installerStatus.current_part} into the PS5 drive, and wait. The installer will automatically detect it and resume copying.`
      : `Waiting for package part ${installerStatus.current_part} of ${installerStatus.total_parts}. Please connect the USB drive containing Part ${installerStatus.current_part}. The installer will automatically detect the file and continue.`;

    const statusBadge = isDiscSource ? 'Scanning /mnt/disc...' : `Waiting for Part ${installerStatus.current_part}...`;

    return (
      <div className="fixed inset-0 z-50 bg-[#0a0a0f] text-white flex flex-col items-center justify-center p-6 overflow-hidden select-none">
        <div className="relative z-10 flex flex-col items-center max-w-2xl w-full text-center">
          {/* Big Icon */}
          <div className="w-28 h-28 rounded-[2px] bg-[#141520] border border-amber-500/40 flex items-center justify-center text-amber-400 mb-6">
            {isDiscSource ? (
              <svg className="w-16 h-16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75">
                <circle cx="12" cy="12" r="10" />
                <circle cx="12" cy="12" r="3" />
                <line x1="12" y1="2" x2="12" y2="5" />
                <line x1="12" y1="19" x2="12" y2="22" />
              </svg>
            ) : (
              <svg className="w-16 h-16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                <path d="M9 2h6v5H9z" />
                <rect x="10.5" y="3.5" width="1" height="1.5" fill="currentColor" stroke="none" />
                <rect x="12.5" y="3.5" width="1" height="1.5" fill="currentColor" stroke="none" />
                <rect x="6" y="7" width="12" height="14" rx="2" />
                <line x1="9" y1="11" x2="15" y2="11" />
              </svg>
            )}
          </div>

          <div className="inline-block px-3 py-1 rounded-[2px] bg-amber-500/20 text-amber-300 border border-amber-500/30 text-xs uppercase font-bold tracking-wider mb-3">
            {isDiscSource
              ? `Disc ${installerStatus.current_part} of ${installerStatus.total_parts}`
              : `Part ${installerStatus.current_part} of ${installerStatus.total_parts}`}
          </div>

          {installerStatus.title_name ? (
            <p className="text-sm font-bold text-zinc-300 mb-1 truncate max-w-lg">
              {installerStatus.title_name}
            </p>
          ) : null}

          <h2 className="text-3xl font-black text-white">{discTitle}</h2>
          <p className="text-sm text-zinc-300 mt-3 max-w-lg leading-relaxed">{discDesc}</p>

          <div className="mt-6 flex items-center space-x-2 bg-black/40 px-4 py-2 rounded-[2px] border border-white/10 text-xs font-mono text-zinc-400">
            <span className="w-2 h-2 rounded-full bg-amber-400" />
            <span>{statusBadge}</span>
          </div>

          <div className="mt-8">
            <button
              type="button"
              onClick={handleCancel}
              className="px-6 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-rose-600/80 border border-white/20 text-sm font-semibold text-zinc-200 hover:text-white transition-all cursor-pointer"
            >
              Cancel Installation
            </button>
          </div>
        </div>
      </div>
    );
  }

  // ──────────────────────────────────────────────────────────────────────────
  // VIEW B: Full-Screen Active Installation Overlay
  // Completely hides background to prevent gamepad focus on elements underneath
  // ──────────────────────────────────────────────────────────────────────────
  if (isInstalling) {
    const isBatch = !!(batchInstall && batchInstall.combinedTotal > 0);
    let totalBytes = installerStatus.total_bytes;
    let downloadedBytes = installerStatus.downloaded_bytes;
    let progressVal = installerStatus.progress;
    let titleToDisplay = installerStatus.title_name || 'Installing Package...';
    let displayIconPath = installerStatus.pkg_path || null;
    let displayIconPkg = displayIconPath
      ? packages.find((p) => p.path === displayIconPath) || null
      : null;
    let statusChip = installerStatus.is_multipart
      ? `Installing Part ${installerStatus.current_part} of ${installerStatus.total_parts}`
      : 'Installing to PS5';

    if (isBatch) {
      totalBytes = batchInstall.combinedTotal;
      if (batchInstall.stage === 'base') {
        downloadedBytes = Math.min(batchInstall.baseSize, installerStatus.downloaded_bytes);
        statusChip = `Installing Base + Update (Part 1/2: Base Package)`;
      } else {
        downloadedBytes = batchInstall.baseSize + Math.min(batchInstall.updateSize, installerStatus.downloaded_bytes);
        statusChip = `Installing Base + Update (Part 2/2: Update)`;
      }
      progressVal = totalBytes > 0 ? (downloadedBytes / totalBytes) * 100 : 0;
      if (batchInstall.titleName) {
        titleToDisplay = batchInstall.titleName;
      }
      if (batchInstall.iconPath) {
        displayIconPath = batchInstall.iconPath;
        displayIconPkg = packages.find((p) => p.path === displayIconPath) || null;
      }
    }
    const displayIconUrl = iconUrlFor(
      displayIconPath,
      displayIconPkg ? displayIconPkg.mtime : 0,
      displayIconPkg ? displayIconPkg.file_size : 0
    );

    return (
      <div className="fixed inset-0 z-50 bg-[#0a0a0f] text-white flex flex-col items-center justify-center p-6 overflow-hidden select-none">
        <div className="relative z-10 flex flex-col items-center max-w-4xl w-full text-center">
          {/* Status Chip */}
          <div className="px-3 py-1 rounded-[2px] bg-blue-500/20 text-blue-300 border border-blue-500/30 text-xs uppercase font-bold tracking-wider mb-5">
            {statusChip}
          </div>

          {/* Big Package Picture */}
          <div className="relative w-48 h-48 sm:w-60 sm:h-60 rounded-[2px] overflow-hidden bg-[#141520] border border-white/20 flex items-center justify-center">
            {/* Fallback Icon */}
            <div className="absolute inset-0 flex items-center justify-center text-zinc-600 pointer-events-none">
              <svg className="w-16 h-16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                <rect x="2" y="3" width="20" height="14" rx="2" />
                <line x1="8" y1="21" x2="16" y2="21" />
                <line x1="12" y1="17" x2="12" y2="21" />
              </svg>
            </div>
            {displayIconUrl ? (
              <BlurIcon
                path={displayIconPath}
                pkg={displayIconPkg}
                alt={titleToDisplay}
                priority={true}
                imgClassName="absolute inset-0 w-full h-full object-cover z-10"
              />
            ) : null}
          </div>

          {/* Title Below Package Picture */}
          <h2 className="text-2xl sm:text-3xl font-black text-white mt-6 max-w-2xl truncate">
            {titleToDisplay}
          </h2>

          <p className="text-xs sm:text-sm font-mono text-zinc-400 mt-1">
            {installerStatus.title_id}
            {installerStatus.content_id ? ` • ${installerStatus.content_id}` : ''}
          </p>

          {installerStatus.prompt_message ? (
            <p className="text-xs text-blue-300 font-medium mt-1">
              {installerStatus.prompt_message}
            </p>
          ) : null}

          {/* Wide Progress Bar Across ~3/4 Screen */}
          <div className="w-[75vw] max-w-3xl bg-white/10 rounded-[2px] h-4 sm:h-5 mt-8 overflow-hidden border border-white/20 p-0.5">
            <div
              className="bg-[#0070d1] h-full rounded-[2px] transition-all duration-300"
              style={{ width: `${Math.min(100, Math.max(0, progressVal))}%` }}
            />
          </div>

          {/* Progress Stats & ETA */}
          <div className="w-[75vw] max-w-3xl flex items-center justify-between text-xs sm:text-sm text-zinc-300 mt-3 px-1">
            <span className="font-bold text-white">
              {progressVal.toFixed(1)}%
              <span className="text-zinc-400 font-normal ml-2">
                ({formatBytes(downloadedBytes)} / {formatBytes(totalBytes)})
              </span>
            </span>

            <span>
              {etaInfo?.text ? (
                <span>
                  <span className="font-medium text-white">{etaInfo.text}</span>
                  {etaInfo.speedStr ? (
                    <span className="text-zinc-400 font-mono ml-2">({etaInfo.speedStr})</span>
                  ) : null}
                </span>
              ) : (
                <span className="text-zinc-400 font-mono capitalize">{installerStatus.status}</span>
              )}
            </span>
          </div>

          {/* Cancel Installation Button */}
          <div className="mt-8">
            <button
              type="button"
              onClick={handleCancel}
              className="px-6 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-rose-600/80 border border-white/20 text-sm font-semibold text-zinc-200 hover:text-white transition-all cursor-pointer"
            >
              Cancel Installation
            </button>
          </div>
        </div>
      </div>
    );
  }

  // ──────────────────────────────────────────────────────────────────────────
  // VIEW C: Full-Screen Refresh / Scan Progress Overlay
  // Completely hides background to prevent gamepad focus on elements underneath
  // ──────────────────────────────────────────────────────────────────────────
  if (refreshing || scanStatus.is_scanning) {
    const totalFiles = scanStatus.total_files || 0;
    const processedFiles = scanStatus.processed_files || 0;
    const currentFile = scanStatus.current_file || '';
    const currentDrive = scanStatus.current_drive || '';
    const hasCounts = totalFiles > 0;
    const percent = hasCounts ? Math.min(100, Math.round((processedFiles / totalFiles) * 100)) : null;

    return (
      <div className="fixed inset-0 z-50 bg-[#0a0a0f] text-white flex flex-col items-center justify-center p-6 select-none">
        <div className="flex flex-col items-center max-w-md w-full text-center space-y-6">
          <div className="ps5-robust-spinner" />

          <div>
            <h2 className="text-xl font-bold text-white">
              Scanning Storage Media...
            </h2>
            <p className="text-xs text-zinc-400 mt-1 truncate">
              {currentDrive || 'Scanning connected drives...'}
            </p>
          </div>

          {/* Simple solid progress bar */}
          <div className="w-full bg-[#141520] border border-white/10 rounded-[2px] p-4 space-y-2.5">
            <div className="flex items-center justify-between text-xs font-mono">
              <span className="text-zinc-400 truncate max-w-[70%]">
                {currentFile ? currentFile : 'Indexing packages...'}
              </span>
              <span className="text-white font-bold">
                {percent !== null ? `${percent}%` : ''}
              </span>
            </div>

            <div className="w-full bg-black/60 h-2 rounded-[2px] overflow-hidden border border-white/10">
              <div
                className="bg-blue-600 h-full rounded-[2px]"
                style={{ width: `${percent !== null ? percent : 0}%` }}
              />
            </div>

            {hasCounts ? (
              <div className="text-right text-[11px] font-mono text-zinc-500">
                {processedFiles} / {totalFiles} pkgs
              </div>
            ) : null}
          </div>
        </div>
      </div>
    );
  }

  // ──────────────────────────────────────────────────────────────────────────
  // MAIN VIEW (Drives, Package Grid, or Title Detail View)
  // ──────────────────────────────────────────────────────────────────────────
  return (
    <div className="min-h-screen bg-[#0a0a0f] text-white flex flex-col font-ps5">
      {/* Toast Notification */}
      {notification && (
        <div
          className={`fixed top-4 right-4 z-50 px-5 py-3 rounded-[2px] flex items-center space-x-3 transition-all duration-300 ${
            notification.type === 'success'
              ? 'bg-emerald-900/90 border border-emerald-500/50 text-emerald-100'
              : notification.type === 'error'
              ? 'bg-rose-900/90 border border-rose-500/50 text-rose-100'
              : notification.type === 'warning'
              ? 'bg-amber-900/90 border border-amber-500/50 text-amber-100'
              : 'bg-blue-900/90 border border-blue-500/50 text-blue-100'
          }`}
        >
          <span className="text-sm font-medium">{notification.message}</span>
        </div>
      )}

      {/* Top Header Bar (Non-sticky, hides naturally when scrolling down) */}
      <header className="border-b border-white/10 bg-[#12131a] px-4 py-3 sm:px-6">
        <div className="w-full flex items-center justify-between">
          <div className="flex items-center space-x-3">
            <div className="w-10 h-10 rounded-[2px] bg-[#0095ff] flex items-center justify-center border border-white/20 shrink-0">
              <svg className="w-6 h-6 text-white" viewBox="0 0 452.23 512" fill="currentColor">
                <path d="M241.45,506.44c-10.13,7.49-20.64,7.22-30.03.38L12.99,392.22C5.05,387.64,0,381.06,0,371.4l.08-234.47c0-8.9,7.83-15.2,15.02-18.6L212.58,4.33c8.76-5.77,18.31-5.77,27.07,0l197.48,114c7.19,3.4,15.01,9.7,15.02,18.6l.08,234.47c0,9.66-5.05,16.24-12.99,20.83l-197.78,114.21ZM320.91,175.85l62.59-35.82L226.06,49.07l-62.16,36.13,157.01,90.65ZM226.31,229.78l62.36-36.05-156.49-90.43-63.46,36.82,157.59,89.66ZM203.22,449.33l.12-180.25L45.62,179.46l-.02,179.06,157.62,90.82ZM406.67,358.43l-.09-178.99-157.75,89.74.12,180.28,157.72-91.03Z" />
              </svg>
            </div>
            <h1 className="text-lg font-bold tracking-wide text-white">
              PKG Manager
            </h1>
          </div>

          <div className="flex items-center space-x-4">
            {/* Storage Display Widget */}
            {storage && (
              <div className="flex items-center space-x-3 bg-white/5 px-3.5 py-1.5 rounded-[2px] border border-white/10 text-xs">
                <svg className="w-4 h-4 text-blue-400 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <rect x="2" y="3" width="20" height="18" rx="2" />
                  <path d="M4 7h16M4 12h16M4 17h16" />
                </svg>
                <div>
                  <div className="text-[11px] text-zinc-300 flex items-center space-x-1.5 whitespace-nowrap">
                    <span className="text-zinc-400">Storage:</span>
                    <span className="font-bold text-white">{formatBytes(storage.free)} Free</span>
                    <span className="text-zinc-500">/ {formatBytes(storage.total)}</span>
                  </div>
                  <div className="w-32 bg-black/60 h-1.5 rounded-[2px] overflow-hidden mt-1 border border-white/10">
                    <div
                      className="bg-[#0070d1] h-full rounded-[2px]"
                      style={{
                        width: `${Math.min(100, Math.max(0, storage.total ? (storage.used / storage.total) * 100 : 0))}%`
                      }}
                    />
                  </div>
                </div>
              </div>
            )}

            {/* Settings Button */}
            <button
              type="button"
              onClick={() => {
                if (showSmbPage) {
                  setShowSmbPage(false);
                  setShowSettings(true);
                  return;
                }
                if (!showSettings) {
                  fetchCacheStats();
                }
                setShowSettings((prev) => !prev);
              }}
              className={`px-3.5 py-1.5 rounded-[2px] ps5-focus-item border text-xs font-semibold transition-colors flex items-center space-x-1.5 cursor-pointer ${
                (showSettings || showSmbPage)
                  ? 'bg-white/20 border-white/40 text-white'
                  : 'bg-white/10 hover:bg-white/15 border-white/10 text-zinc-200'
              }`}
            >
              <svg className="w-3.5 h-3.5 text-zinc-300" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <circle cx="12" cy="12" r="3" />
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2 2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
              </svg>
              <span>Settings</span>
            </button>

            {/* Rescan Button */}
            <button
              type="button"
              onClick={refreshAll}
              disabled={refreshing}
              className="px-3.5 py-1.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 border border-white/10 text-xs font-semibold text-white transition-colors flex items-center space-x-2 disabled:opacity-50 cursor-pointer"
            >
              <svg
                className={`w-3.5 h-3.5 text-zinc-300 ${refreshing ? 'animate-spin' : ''}`}
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                strokeWidth="2"
              >
                <path d="M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
                <path d="M3 3v5h5" />
                <path d="M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16" />
                <path d="M16 21h5v-5" />
              </svg>
              <span>{refreshing ? 'Scanning...' : 'Rescan'}</span>
            </button>
          </div>
        </div>
      </header>

      {/* Main Container */}
      <main className="w-full px-4 py-4 flex-1 space-y-6">
        {showSmbPage ? (
          <div className="max-w-6xl mx-auto px-4 sm:px-8 space-y-6 pb-12">
            {/* Return Button & Breadcrumbs at top */}
            <div className="flex items-center justify-between border-b border-white/10 pb-4">
              <div className="flex items-center space-x-4 min-w-0 flex-1">
                <button
                  type="button"
                  onClick={() => setShowSmbPage(false)}
                  className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 border border-white/10 text-base font-semibold transition-all flex items-center space-x-2 text-zinc-200 shrink-0 cursor-pointer"
                >
                  <span>&larr;</span>
                  <span>Back to Settings</span>
                </button>

                <div className="text-sm text-zinc-400 flex items-center space-x-2 min-w-0">
                  <span className="shrink-0">PKG Manager</span>
                  <span className="shrink-0 text-zinc-600">&rsaquo;</span>
                  <button
                    type="button"
                    onClick={() => setShowSmbPage(false)}
                    className="shrink-0 text-zinc-400 hover:text-white transition-colors cursor-pointer"
                  >
                    Settings
                  </button>
                  <span className="shrink-0 text-zinc-600">&rsaquo;</span>
                  <span className="text-white font-semibold truncate">Samba Shares</span>
                </div>
              </div>

              <button
                type="button"
                onClick={() => {
                  setSmbEditIndex(-1);
                  setSmbForm({
                    id: '',
                    label: '',
                    server: '',
                    port: 445,
                    share: '',
                    path: '',
                    username: '',
                    password: '',
                    workgroup: 'WORKGROUP',
                    is_read_only: false,
                    enabled: true
                  });
                  setSmbTestResult(null);
                  setShowSmbModal(true);
                }}
                className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-cyan-600 hover:bg-cyan-500 text-white text-sm font-bold transition-colors flex items-center space-x-1.5 cursor-pointer shrink-0"
              >
                <span>+ Add Share</span>
              </button>
            </div>

            {/* Shares Header Card */}
            <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4">
              <div className="flex items-center space-x-4">
                <div className="w-12 h-12 rounded-[2px] bg-cyan-600/20 border border-cyan-500/30 flex items-center justify-center text-cyan-400 shrink-0">
                  <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <rect x="2" y="2" width="20" height="8" rx="2" />
                    <rect x="2" y="14" width="20" height="8" rx="2" />
                    <line x1="6" y1="6" x2="6.01" y2="6" />
                    <line x1="6" y1="18" x2="6.01" y2="18" />
                    <path d="M12 10v4" />
                  </svg>
                </div>
                <div>
                  <h2 className="text-xl font-bold text-white">Samba (SMB) Shares</h2>
                  <p className="text-xs text-zinc-400 mt-0.5">
                    Browse and install packages over local network
                  </p>
                </div>
              </div>

              <div className="flex items-center space-x-2 text-xs font-mono">
                <span className="px-3 py-1 rounded-[2px] bg-white/5 border border-white/10 text-zinc-300">
                  {settings.smb_shares?.length || 0} {settings.smb_shares?.length === 1 ? 'Share' : 'Shares'}
                </span>
                <span className="px-3 py-1 rounded-[2px] bg-emerald-500/15 text-emerald-300 border border-emerald-500/30">
                  {(settings.smb_shares || []).filter((s) => s.enabled).length} Enabled
                </span>
              </div>
            </div>

            {/* Shares List */}
            {(!settings.smb_shares || settings.smb_shares.length === 0) ? (
              <div className="py-16 text-center rounded-[2px] border border-white/10 bg-[#12131a]/40 p-8 space-y-4">
                <div className="w-16 h-16 rounded-[2px] bg-white/5 border border-white/10 mx-auto flex items-center justify-center text-zinc-500">
                  <svg className="w-8 h-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                    <rect x="2" y="2" width="20" height="8" rx="2" />
                    <rect x="2" y="14" width="20" height="8" rx="2" />
                    <line x1="6" y1="6" x2="6.01" y2="6" />
                    <line x1="6" y1="18" x2="6.01" y2="18" />
                    <path d="M12 10v4" />
                  </svg>
                </div>
                <h3 className="text-lg font-bold text-white">No Samba Shares Configured</h3>
                <p className="text-xs text-zinc-400 max-w-md mx-auto leading-relaxed">
                  Connect to a Windows PC or NAS Samba share to install packages directly over your local network.
                </p>
                <button
                  type="button"
                  onClick={() => {
                    setSmbEditIndex(-1);
                    setSmbForm({
                      id: '',
                      label: '',
                      server: '',
                      port: 445,
                      share: '',
                      path: '',
                      username: '',
                      password: '',
                      workgroup: 'WORKGROUP',
                      is_read_only: false,
                      enabled: true
                    });
                    setSmbTestResult(null);
                    setShowSmbModal(true);
                  }}
                  className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-cyan-600 hover:bg-cyan-500 text-white font-bold text-xs transition-colors cursor-pointer"
                >
                  + Add Samba Share
                </button>
              </div>
            ) : (
              <div className="space-y-4">
                {settings.smb_shares.map((sh, idx) => {
                  const portStr = (sh.port && sh.port !== 445) ? `:${sh.port}` : '';
                  const pathStr = sh.path ? `/${sh.path.replace(/^\/+/, '')}` : '';
                  const urlStr = `smb://${sh.server}${portStr}/${sh.share}${pathStr}`;
                  return (
                    <div
                      key={sh.id || idx}
                      className={`rounded-[2px] p-5 border transition-all ${
                        sh.enabled ? 'bg-[#141520] border-white/10' : 'bg-[#12131b]/60 border-white/5 opacity-50'
                      }`}
                    >
                      <div className="flex flex-col md:flex-row md:items-center justify-between gap-4">
                        <div className="min-w-0 flex-1">
                          <div className="flex items-center space-x-3 flex-wrap gap-y-1">
                            <h4 className="text-base font-bold text-white truncate">
                              {sh.label || `${sh.server}/${sh.share}`}
                            </h4>
                            <span className={`text-[10px] px-2.5 py-0.5 rounded-[2px] font-semibold border shrink-0 ${
                              sh.is_read_only
                                ? 'bg-amber-500/15 text-amber-300 border-amber-500/30'
                                : 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30'
                            }`}>
                              {sh.is_read_only ? 'Read-Only' : 'Read/Write'}
                            </span>
                            {!sh.enabled && (
                              <span className="text-[10px] px-2 py-0.5 rounded-[2px] font-semibold bg-zinc-800 text-zinc-400 border border-zinc-700 shrink-0">
                                Disabled
                              </span>
                            )}
                            {sh.username && (
                              <span className="text-[10px] px-2 py-0.5 rounded-[2px] font-mono bg-white/5 text-zinc-400 border border-white/10 shrink-0">
                                user: {sh.username}
                              </span>
                            )}
                          </div>
                          <p className="text-xs font-mono text-cyan-300/80 mt-1.5 truncate select-all">{urlStr}</p>
                        </div>

                        {/* Actions */}
                        <div className="flex items-center space-x-2 shrink-0">
                          <button
                            type="button"
                            onClick={() => handleTestSmbConnection(sh)}
                            disabled={smbTesting}
                            className="px-3 py-2 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-200 hover:text-white border border-white/10 text-xs font-semibold transition-colors cursor-pointer flex items-center space-x-1.5"
                          >
                            <span>Test</span>
                          </button>
                          <button
                            type="button"
                            onClick={() => handleToggleSmbShare(idx)}
                            className={`px-3 py-2 rounded-[2px] ps5-focus-item text-xs font-semibold border transition-colors cursor-pointer ${
                              sh.enabled
                                ? 'bg-white/5 text-zinc-300 hover:bg-white/10 border-white/10'
                                : 'bg-cyan-600/20 text-cyan-300 border-cyan-500/30 hover:bg-cyan-600/30'
                            }`}
                          >
                            {sh.enabled ? 'Disable' : 'Enable'}
                          </button>
                          <button
                            type="button"
                            onClick={() => {
                              setSmbEditIndex(idx);
                              setSmbForm({ ...sh });
                              setSmbTestResult(null);
                              setShowSmbModal(true);
                            }}
                            className="px-3 py-2 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-200 hover:text-white border border-white/10 text-xs font-semibold transition-colors cursor-pointer"
                          >
                            Edit
                          </button>
                          <button
                            type="button"
                            onClick={() => handleRemoveSmbShare(idx)}
                            className="px-3 py-2 rounded-[2px] ps5-focus-item bg-rose-600/15 hover:bg-rose-600/25 text-rose-300 border border-rose-500/25 text-xs font-semibold transition-colors cursor-pointer"
                          >
                            Delete
                          </button>
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        ) : showSettings ? (
          <div className="max-w-6xl mx-auto px-4 sm:px-8 space-y-6 pb-12">
            {/* Return Button at top */}
            <div className="flex items-center space-x-4 border-b border-white/10 pb-4">
              <button
                type="button"
                onClick={() => {
                  setShowSettings(false);
                  setShowSmbPage(false);
                }}
                className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 border border-white/10 text-base font-semibold transition-all flex items-center space-x-2 text-zinc-200 shrink-0 cursor-pointer"
              >
                <span>&larr;</span>
                <span>Back</span>
              </button>

              <div className="text-sm text-zinc-400 flex items-center space-x-2 min-w-0">
                <span className="shrink-0">PKG Manager</span>
                <span className="shrink-0 text-zinc-600">&rsaquo;</span>
                <span className="text-white font-semibold truncate">Settings</span>
              </div>
            </div>

            {/* 2-Column Responsive Layout */}
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
              {/* Left Column */}
              <div className="space-y-6">
                {/* Package Organization Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-5">
                  <div className="flex items-center space-x-3 pb-3 border-b border-white/10">
                    <div className="w-10 h-10 rounded-[2px] bg-purple-600/20 border border-purple-500/30 flex items-center justify-center text-purple-400 shrink-0">
                      <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                        <line x1="8" y1="6" x2="21" y2="6" />
                        <line x1="8" y1="12" x2="21" y2="12" />
                        <line x1="8" y1="18" x2="21" y2="18" />
                        <line x1="3" y1="6" x2="3.01" y2="6" />
                        <line x1="3" y1="12" x2="3.01" y2="12" />
                        <line x1="3" y1="18" x2="3.01" y2="18" />
                      </svg>
                    </div>
                    <div>
                      <h3 className="text-lg font-bold text-white">Library Display</h3>
                      <p className="text-xs text-zinc-400">Display &amp; sorting preferences</p>
                    </div>
                  </div>

                  <button
                    type="button"
                    onClick={() => handleSaveSettings({ ...settings, fade_installed_packages: !settings.fade_installed_packages })}
                    className="w-full bg-white/5 hover:bg-white/10 border border-white/10 rounded-[2px] ps5-focus-item p-4 flex items-center justify-between transition-colors cursor-pointer text-left"
                  >
                    <div className="min-w-0 flex-1 mr-4">
                      <span className="text-sm font-semibold text-white block">Fade out installed packages</span>
                      <span className="text-xs text-zinc-400 block mt-1">
                        Dim titles and DLCs already installed on this console.
                      </span>
                    </div>
                    <div className={`w-6 h-6 rounded-[2px] border flex items-center justify-center shrink-0 transition-colors ${
                      settings.fade_installed_packages
                        ? 'bg-blue-600 border-blue-500 text-white'
                        : 'bg-black/40 border-white/20 text-transparent'
                    }`}>
                      <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                    </div>
                  </button>

                  <button
                    type="button"
                    onClick={() => handleSaveSettings({ ...settings, move_installed_to_end: !settings.move_installed_to_end })}
                    className="w-full bg-white/5 hover:bg-white/10 border border-white/10 rounded-[2px] ps5-focus-item p-4 flex items-center justify-between transition-colors cursor-pointer text-left"
                  >
                    <div className="min-w-0 flex-1 mr-4">
                      <span className="text-sm font-semibold text-white block">Move installed packages to end</span>
                      <span className="text-xs text-zinc-400 block mt-1">
                        Place fully installed titles at the bottom of the list.
                      </span>
                    </div>
                    <div className={`w-6 h-6 rounded-[2px] border flex items-center justify-center shrink-0 transition-colors ${
                      settings.move_installed_to_end
                        ? 'bg-blue-600 border-blue-500 text-white'
                        : 'bg-black/40 border-white/20 text-transparent'
                    }`}>
                      <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                    </div>
                  </button>

                  <button
                    type="button"
                    onClick={() => handleSaveSettings({ ...settings, all_sources_mode: !settings.all_sources_mode })}
                    className="w-full bg-white/5 hover:bg-white/10 border border-white/10 rounded-[2px] ps5-focus-item p-4 flex items-center justify-between transition-colors cursor-pointer text-left"
                  >
                    <div className="min-w-0 flex-1 mr-4">
                      <span className="text-sm font-semibold text-white block">List all packages automatically</span>
                      <span className="text-xs text-zinc-400 block mt-1">
                        Show all packages across all storage sources on launch.
                      </span>
                    </div>
                    <div className={`w-6 h-6 rounded-[2px] border flex items-center justify-center shrink-0 transition-colors ${
                      settings.all_sources_mode
                        ? 'bg-blue-600 border-blue-500 text-white'
                        : 'bg-black/40 border-white/20 text-transparent'
                    }`}>
                      <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                    </div>
                  </button>
                </div>

                {/* Samba (SMB) Shares Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-4">
                  <div className="flex items-center justify-between pb-3 border-b border-white/10">
                    <div className="flex items-center space-x-3">
                      <div className="w-10 h-10 rounded-[2px] bg-cyan-600/20 border border-cyan-500/30 flex items-center justify-center text-cyan-400 shrink-0">
                        <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                          <rect x="2" y="2" width="20" height="8" rx="2" />
                          <rect x="2" y="14" width="20" height="8" rx="2" />
                          <line x1="6" y1="6" x2="6.01" y2="6" />
                          <line x1="6" y1="18" x2="6.01" y2="18" />
                          <path d="M12 10v4" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-lg font-bold text-white">Samba (SMB) Shares</h3>
                        <p className="text-xs text-zinc-400">
                          {settings.smb_shares?.length || 0} share{settings.smb_shares?.length === 1 ? '' : 's'} configured
                        </p>
                      </div>
                    </div>
                  </div>

                  <p className="text-xs text-zinc-300 leading-relaxed">
                    Connect to local network shares (PC or NAS) to browse and install packages remotely.
                  </p>

                  <button
                    type="button"
                    onClick={() => setShowSmbPage(true)}
                    className="w-full py-3 px-4 rounded-[2px] ps5-focus-item bg-cyan-600/20 hover:bg-cyan-600/30 border border-cyan-500/30 text-cyan-200 text-sm font-bold transition-colors flex items-center justify-between cursor-pointer"
                  >
                    <span>Manage Samba Shares</span>
                    <span>&rarr;</span>
                  </button>
                </div>

                {/* Home Screen Shortcut Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-4">
                  <div className="flex items-center justify-between pb-3 border-b border-white/10">
                    <div className="flex items-center space-x-3">
                      <div className="w-10 h-10 rounded-[2px] bg-blue-600/20 border border-blue-500/30 flex items-center justify-center text-blue-400 shrink-0">
                        <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                          <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
                          <polyline points="9 22 9 12 15 12 15 22" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-lg font-bold text-white">Home Screen Shortcut</h3>
                        <p className="text-xs text-zinc-400">Media tab quick launch</p>
                      </div>
                    </div>

                    <button
                      type="button"
                      onClick={handleInstallShortcut}
                      disabled={installingShortcut}
                      className="px-4 py-2 rounded-[2px] ps5-focus-item bg-blue-600 hover:bg-blue-500 text-white text-xs font-bold transition-colors cursor-pointer flex items-center space-x-2 disabled:opacity-50"
                    >
                      {installingShortcut ? (
                        <>
                          <div className="ps5-robust-spinner-sm" />
                          <span>Installing...</span>
                        </>
                      ) : (
                        <span>Install Shortcut</span>
                      )}
                    </button>
                  </div>

                  <p className="text-xs text-zinc-300 leading-relaxed">
                    Adds a shortcut to the PS5 Media tab to launch PKG Manager directly from the home screen.
                  </p>
                </div>

                {/* Support & Donations Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-4">
                  <div className="flex items-center space-x-3 pb-3 border-b border-white/10">
                    <div className="w-10 h-10 rounded-[2px] bg-rose-600/20 border border-rose-500/30 flex items-center justify-center text-rose-400 shrink-0">
                      <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                        <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z" />
                      </svg>
                    </div>
                    <div>
                      <h3 className="text-lg font-bold text-white">Support the Project</h3>
                      <p className="text-xs text-zinc-400">Donations &amp; contributions</p>
                    </div>
                  </div>

                  <p className="text-xs text-zinc-300 leading-relaxed">
                    If you find PKG Manager useful and would like to support its continued development or say thanks, donations are greatly appreciated!
                  </p>

                  {isPlayStation ? (
                    <div className="bg-black/30 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                      <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                        <QRCodeSVG
                          value={DONATE_URL}
                          size={144}
                          level="M"
                        />
                      </div>
                      <div className="text-center space-y-1">
                        <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                        <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                          github.com/itsPLK/ps5-pkg-manager
                        </span>
                      </div>
                    </div>
                  ) : (
                    <div className="space-y-3">
                      <a
                        href={DONATE_URL}
                        target="_blank"
                        rel="noopener noreferrer"
                        className="w-full py-3 px-4 rounded-[2px] ps5-focus-item bg-rose-600/20 hover:bg-rose-600/30 border border-rose-500/30 text-rose-200 text-sm font-bold transition-colors flex items-center justify-center cursor-pointer"
                      >
                        View donation options
                      </a>
                      <div className="flex justify-center">
                        <button
                          type="button"
                          onClick={() => setShowDonateQr((prev) => !prev)}
                          className="text-[11px] text-zinc-400 hover:text-zinc-200 transition-colors cursor-pointer underline underline-offset-2"
                        >
                          {showDonateQr ? 'Hide QR Code' : 'Show QR Code for phone scan'}
                        </button>
                      </div>
                      {showDonateQr && (
                        <div className="bg-black/30 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                          <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                            <QRCodeSVG
                              value={DONATE_URL}
                              size={144}
                              level="M"
                            />
                          </div>
                          <div className="text-center space-y-1">
                            <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                            <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                              github.com/itsPLK/ps5-pkg-manager
                            </span>
                          </div>
                        </div>
                      )}
                    </div>
                  )}
                </div>
              </div>

              {/* Right Column */}
              <div className="space-y-6">
                {/* Package Cache Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-4">
                  <div className="flex items-center justify-between pb-3 border-b border-white/10">
                    <div className="flex items-center space-x-3">
                      <div className="w-10 h-10 rounded-[2px] bg-emerald-600/20 border border-emerald-500/30 flex items-center justify-center text-emerald-400 shrink-0">
                        <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                          <rect x="2" y="2" width="20" height="8" rx="2" ry="2" />
                          <rect x="2" y="14" width="20" height="8" rx="2" ry="2" />
                          <line x1="6" y1="6" x2="6.01" y2="6" />
                          <line x1="6" y1="18" x2="6.01" y2="18" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-lg font-bold text-white">Cache</h3>
                        <p className="text-xs text-zinc-400">Metadata &amp; icons</p>
                      </div>
                    </div>

                    {cacheStats && cacheStats.total_count > 0 && (
                      <button
                        type="button"
                        onClick={() => setShowClearCacheModal(true)}
                        className="px-4 py-2 rounded-[2px] ps5-focus-item bg-rose-600/20 hover:bg-rose-600/30 border border-rose-500/30 text-rose-300 text-xs font-bold transition-colors cursor-pointer"
                      >
                        Clear Cache...
                      </button>
                    )}
                  </div>

                  {/* Cache Overview Banner */}
                  <div className="bg-black/30 border border-white/5 rounded-[2px] p-4 flex items-center justify-between">
                    <div>
                      <span className="text-xs text-zinc-400 uppercase tracking-wider font-semibold block">Cache Size</span>
                      <span className="text-xl font-bold text-white font-mono mt-0.5 block">
                        {loadingStats ? 'Checking...' : formatBytes(cacheStats?.total_bytes || 0)}
                      </span>
                    </div>
                    <div className="text-right">
                      <span className="text-xs text-zinc-400 uppercase tracking-wider font-semibold block">Cached Packages</span>
                      <span className="text-xl font-bold text-blue-400 font-mono mt-0.5 block">
                        {loadingStats ? '...' : (cacheStats?.total_count || 0)}
                      </span>
                    </div>
                  </div>

                  {/* Cache Directory Location */}
                  <div className="bg-white/5 border border-white/10 rounded-[2px] p-3 flex items-center justify-between">
                    <div className="min-w-0 flex-1">
                      <span className="text-xs font-semibold text-zinc-400 block">Location</span>
                      <span className="text-xs font-mono text-zinc-200 block truncate mt-0.5">
                        {cacheStats?.cache_path || '/data/pkgmgr/cache'}
                      </span>
                    </div>
                  </div>
                </div>

                {/* Orphaned Leftovers Cleanup Card */}
                <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 space-y-4">
                  <div className="flex items-center justify-between pb-3 border-b border-white/10">
                    <div className="flex items-center space-x-3">
                      <div className="w-10 h-10 rounded-[2px] bg-amber-600/20 border border-amber-500/30 flex items-center justify-center text-amber-400 shrink-0">
                        <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                          <path strokeLinecap="round" strokeLinejoin="round" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                        </svg>
                      </div>
                      <div>
                        <h3 className="text-lg font-bold text-white">Leftover Cleanup</h3>
                        <p className="text-xs text-zinc-400">Orphaned updates &amp; DLCs</p>
                      </div>
                    </div>

                    <button
                      type="button"
                      onClick={handleScanLeftovers}
                      disabled={scanningLeftovers}
                      className="px-4 py-2 rounded-[2px] ps5-focus-item bg-amber-600/20 hover:bg-amber-600/30 border border-amber-500/30 text-amber-300 text-xs font-bold transition-colors cursor-pointer disabled:opacity-50 flex items-center space-x-2 shrink-0"
                    >
                      {scanningLeftovers ? (
                        <>
                          <div className="ps5-robust-spinner-sm" />
                          <span>Scanning...</span>
                        </>
                      ) : (
                        <span>{leftoversData ? 'Rescan' : 'Scan'}</span>
                      )}
                    </button>
                  </div>

                  {/* Body Content */}
                  {leftoversData === null && !scanningLeftovers ? (
                    <div className="bg-black/30 border border-white/5 rounded-[2px] p-4 text-center">
                      <p className="text-xs text-zinc-400 leading-relaxed">
                        Scan console storage for leftover updates or DLCs from uninstalled base packages.
                      </p>
                    </div>
                  ) : scanningLeftovers ? (
                    <div className="py-8 text-center space-y-3">
                      <div className="ps5-robust-spinner mx-auto" />
                      <p className="text-xs text-zinc-400">Scanning for leftover files...</p>
                    </div>
                  ) : leftoversData?.count === 0 ? (
                    <div className="bg-emerald-500/10 border border-emerald-500/20 rounded-[2px] p-4 flex items-center space-x-3 text-emerald-300">
                      <svg className="w-5 h-5 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                        <polyline points="20 6 9 17 4 12" />
                      </svg>
                      <span className="text-xs font-medium">Console storage is clean. No orphaned packages found.</span>
                    </div>
                  ) : (
                    <div className="space-y-3">
                      <div className="flex items-center justify-between text-xs text-zinc-400 px-1">
                        <span>
                          Found <strong className="text-white">{leftoversData.count}</strong> orphaned {leftoversData.count === 1 ? 'package' : 'packages'}
                        </span>
                        <span>
                          Total:{' '}
                          <strong className="text-amber-400 font-mono">
                            {formatBytes(leftoversData.leftovers.reduce((acc, x) => acc + (x.total_size || 0), 0))}
                          </strong>
                        </span>
                      </div>

                      <div className="space-y-2.5 max-h-72 overflow-y-auto pr-1">
                        {leftoversData.leftovers.map((item) => (
                          <div
                            key={item.title_id}
                            className="bg-white/5 border border-white/10 rounded-[2px] p-3.5 flex items-center justify-between space-x-3 hover:border-white/20 transition-colors"
                          >
                            <div className="flex items-center space-x-3 min-w-0 flex-1">
                              <div className="w-10 h-10 rounded-[2px] bg-amber-500/10 border border-amber-500/20 shrink-0 flex items-center justify-center text-amber-400">
                                <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75">
                                  <path strokeLinecap="round" strokeLinejoin="round" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                                </svg>
                              </div>

                              <div className="min-w-0 flex-1">
                                <div className="flex items-center space-x-2">
                                  <span className="text-sm font-bold text-white truncate">{item.title_name}</span>
                                  <span className="text-[10px] font-mono px-1.5 py-0.5 rounded-[2px] bg-black/40 text-zinc-400 border border-white/10 shrink-0">
                                    {item.title_id}
                                  </span>
                                </div>
                                <div className="flex items-center space-x-2 mt-1 flex-wrap">
                                  <span className={`text-[10px] font-bold px-1.5 py-0.5 rounded-[2px] border ${
                                    item.type === 'Orphaned Update'
                                      ? 'bg-purple-950/70 text-purple-300 border-purple-800/40'
                                      : item.type === 'Orphaned DLC'
                                      ? 'bg-emerald-950/70 text-emerald-300 border-emerald-800/40'
                                      : 'bg-amber-950/70 text-amber-300 border-amber-800/40'
                                  }`}>
                                    {item.type} {item.version ? `(${formatVersion(item.version)})` : ''}
                                  </span>
                                  <span className="text-xs font-mono text-zinc-300 font-bold">
                                    {formatBytes(item.total_size)}
                                  </span>
                                  <span className="text-[11px] text-zinc-500">
                                    ({item.paths?.length || 0} locations)
                                  </span>
                                </div>
                              </div>
                            </div>

                            <button
                              type="button"
                              onClick={() => setSelectedLeftoverToDelete(item)}
                              className="px-3 py-1.5 rounded-[2px] ps5-focus-item bg-rose-600/20 hover:bg-rose-600/30 text-rose-300 border border-rose-500/30 text-xs font-bold transition-colors cursor-pointer shrink-0"
                            >
                              Delete...
                            </button>
                          </div>
                        ))}
                      </div>
                    </div>
                  )}
                </div>
              </div>
            </div>
          </div>
        ) : (
          <>
            {/* VIEW 1: Main Drives Page (Simple & Clean) */}
            {!selectedDrive && (
          <div className="space-y-4">
            <div className="flex items-center space-x-3">
              <h2 className="text-xl font-bold text-white">Select Storage Media</h2>
              <span className="text-xs px-2.5 py-0.5 rounded-[2px] bg-white/5 border border-white/10 text-zinc-400">
                {drives.length} Detected
              </span>
            </div>

            {loadingDrives ? (
              <div className="py-24 text-center">
                <div className="ps5-robust-spinner mx-auto" />
                <p className="text-sm text-zinc-400 mt-3">Scanning mounted drives...</p>
              </div>
            ) : drives.length === 0 ? (
              <div className="py-20 text-center rounded-[2px] border border-white/10 bg-[#12131a]/40 p-8">
                <div className="w-20 h-20 rounded-[2px] bg-white/5 border border-white/10 mx-auto flex items-center justify-center text-zinc-500 mb-4">
                  <svg className="w-10 h-10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                    <rect x="2" y="6" width="20" height="12" rx="2" />
                    <circle cx="12" cy="12" r="3" />
                    <path d="M6 12h.01M18 12h.01" />
                  </svg>
                </div>
                <h3 className="text-xl font-bold text-white">No Storage Media Found</h3>
                <p className="text-sm text-zinc-400 max-w-md mx-auto mt-2">
                  No mounted USB drives (/mnt/usb0-7) or Blu-ray discs (/mnt/disc) were detected. Insert a drive with <span className="font-mono text-zinc-200">.pkg</span> files and click Rescan.
                </p>
                <button
                  type="button"
                  onClick={refreshAll}
                  className="mt-6 px-6 py-3 rounded-[2px] ps5-focus-item bg-blue-600 hover:bg-blue-500 text-white font-semibold text-sm transition-colors"
                >
                  Rescan Drives
                </button>
              </div>
            ) : (
              <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
                {/* All Sources Card */}
                <button
                  type="button"
                  onClick={() => handleSelectDrive(ALL_SOURCES_DRIVE)}
                  className="group relative rounded-[2px] ps5-focus-item p-5 transition-all flex items-center space-x-4 border text-left bg-[#141520] border-white/10 hover:border-white/20 hover:bg-[#171824] cursor-pointer"
                >
                  <div className="w-14 h-14 rounded-[2px] flex items-center justify-center border shrink-0 bg-indigo-950/40 border-indigo-500/40 text-indigo-300">
                    <svg className="w-8 h-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                      <rect x="2" y="2" width="20" height="8" rx="2" />
                      <rect x="2" y="14" width="20" height="8" rx="2" />
                      <line x1="6" y1="6" x2="6.01" y2="6" />
                      <line x1="6" y1="18" x2="6.01" y2="18" />
                      <path d="M12 10v4" />
                    </svg>
                  </div>

                  <div className="min-w-0 flex-1">
                    <div className="flex items-center space-x-2">
                      <h3 className="text-base font-bold text-white transition-colors truncate">
                        All Sources
                      </h3>
                      <span className="px-2 py-0.5 rounded-[2px] text-[11px] font-bold border shrink-0 bg-indigo-500/20 text-indigo-300 border-indigo-500/30">
                        Combined
                      </span>
                    </div>
                    <p className="text-xs font-mono text-zinc-400 mt-0.5 truncate">
                      All connected storage &amp; shares
                    </p>
                  </div>
                </button>

                {drives.map((d) => {
                  const isUsb = d.type === 'usb';
                  const isDisc = d.type === 'disc';
                  const isSmb = d.type === 'smb';
                  const isClickable = d.clickable;

                  return (
                    <button
                      key={d.id}
                      type="button"
                      disabled={!isClickable}
                      onClick={() => isClickable && handleSelectDrive(d)}
                      className={`group relative rounded-[2px] p-5 transition-all flex items-center space-x-4 border text-left ${
                        isClickable
                          ? 'ps5-focus-item bg-[#141520] border-white/10 hover:border-white/20 hover:bg-[#171824] cursor-pointer'
                          : 'bg-[#12131b]/60 border-white/5 opacity-40 cursor-not-allowed'
                      }`}
                    >
                      <div
                        className={`w-14 h-14 rounded-[2px] flex items-center justify-center border shrink-0 ${
                          isClickable
                            ? isDisc
                              ? 'bg-purple-950/40 border-purple-500/40 text-purple-300'
                              : isSmb
                              ? 'bg-cyan-950/40 border-cyan-500/40 text-cyan-300'
                              : 'bg-blue-950/40 border-blue-500/40 text-blue-300'
                            : 'bg-white/5 border-white/5 text-zinc-600'
                        }`}
                      >
                        {isDisc ? (
                          /* Blu-ray Disc Icon */
                          <svg className="w-8 h-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75">
                            <circle cx="12" cy="12" r="10" />
                            <circle cx="12" cy="12" r="3" />
                            <line x1="12" y1="2" x2="12" y2="5" />
                            <line x1="12" y1="19" x2="12" y2="22" />
                          </svg>
                        ) : isSmb ? (
                          /* Samba Network Share Icon */
                          <svg className="w-8 h-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                            <rect x="2" y="2" width="20" height="8" rx="2" />
                            <rect x="2" y="14" width="20" height="8" rx="2" />
                            <line x1="6" y1="6" x2="6.01" y2="6" />
                            <line x1="6" y1="18" x2="6.01" y2="18" />
                            <path d="M12 10v4" />
                          </svg>
                        ) : (
                          /* USB Drive Icon */
                          <svg className="w-8 h-8" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round">
                            <path d="M9 2h6v5H9z" />
                            <rect x="10.5" y="3.5" width="1" height="1.5" fill="currentColor" stroke="none" />
                            <rect x="12.5" y="3.5" width="1" height="1.5" fill="currentColor" stroke="none" />
                            <rect x="6" y="7" width="12" height="14" rx="2" />
                            <line x1="9" y1="11" x2="15" y2="11" />
                          </svg>
                        )}
                      </div>

                      <div className="min-w-0 flex-1">
                        <div className="flex items-center space-x-2">
                          <h3 className="text-base font-bold text-white transition-colors truncate">
                            {d.label}
                          </h3>
                          <span
                            className={`px-2 py-0.5 rounded-[2px] text-[11px] font-bold border shrink-0 ${
                              isClickable
                                ? isSmb
                                  ? 'bg-cyan-500/20 text-cyan-300 border-cyan-500/30'
                                  : isDisc
                                  ? 'bg-purple-500/20 text-purple-300 border-purple-500/30'
                                  : 'bg-blue-500/20 text-blue-300 border-blue-500/30'
                                : 'bg-white/5 text-zinc-500 border-white/10'
                            }`}
                          >
                            {d.pkg_count} {d.pkg_count === 1 ? 'PKG' : 'PKGs'}
                          </span>
                        </div>
                        <p className="text-xs font-mono text-zinc-400 mt-0.5 truncate">
                          {d.path}
                        </p>
                      </div>
                    </button>
                  );
                })}
              </div>
            )}
          </div>
        )}

        {/* VIEW 2: Drive Package Grid Page (Grouped Titles with Square Cards) */}
        {selectedDrive && !selectedTitle && (
          <div className="space-y-6">
            {/* Header & Back Button */}
            <div className="flex flex-col md:flex-row md:items-center justify-between space-y-4 md:space-y-0 md:space-x-4 border-b border-white/10 pb-6">
              <div className="flex items-center space-x-4">
                <button
                  type="button"
                  onClick={handleBackToDrives}
                  className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 border border-white/10 text-sm font-semibold transition-all flex items-center space-x-2 text-zinc-200 cursor-pointer shrink-0"
                >
                  <span>&larr;</span>
                  <span>Back to Drives</span>
                </button>

                <div>
                  <h2 className="text-2xl font-black text-white flex items-center space-x-2">
                    <span>{selectedDrive.label}</span>
                    <span className="text-xs px-2.5 py-0.5 rounded-[2px] bg-blue-500/20 text-blue-400 font-mono">
                      {selectedDrive.path}
                    </span>
                  </h2>
                  <p className="text-xs text-zinc-400 mt-0.5">
                    {groupedTitles.length} {groupedTitles.length === 1 ? 'Title' : 'Titles'} ({packages.length} Packages) found
                  </p>
                </div>
              </div>

              {/* Search Bar & Sort Controls */}
              <div className="flex items-center space-x-3 shrink-0">
                <div className="flex items-center bg-[#161722] border border-white/10 rounded-[2px] ps5-focus-item px-3.5 py-2 w-64 focus-within:border-white/30">
                  <svg
                    className="w-4 h-4 text-zinc-500 mr-2.5 shrink-0"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    strokeWidth="2"
                  >
                    <circle cx="11" cy="11" r="8" />
                    <line x1="21" y1="21" x2="16.65" y2="16.65" />
                  </svg>
                  <input
                    type="text"
                    placeholder="Search titles..."
                    value={searchQuery}
                    onChange={(e) => setSearchQuery(e.target.value)}
                    className="bg-transparent border-none outline-none text-sm text-white placeholder-zinc-500 w-full"
                  />
                </div>

                <div className="flex items-center space-x-2 bg-[#161722] border border-white/10 rounded-[2px] ps5-focus-item px-3 py-2 shrink-0">
                  <svg className="w-4 h-4 text-zinc-400 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M3 6h18M6 12h12M10 18h4" />
                  </svg>
                  <label htmlFor="sort-dropdown" className="text-xs text-zinc-400 shrink-0 font-medium">Sort:</label>
                  <select
                    id="sort-dropdown"
                    value={sortBy}
                    onChange={(e) => setSortBy(e.target.value)}
                    className="bg-transparent text-xs text-white focus:outline-none cursor-pointer pr-1"
                  >
                    <option value="date-desc" className="bg-[#161722] text-white">Date (Newest)</option>
                    <option value="date-asc" className="bg-[#161722] text-white">Date (Oldest)</option>
                    <option value="name-asc" className="bg-[#161722] text-white">Title (A-Z)</option>
                    <option value="name-desc" className="bg-[#161722] text-white">Title (Z-A)</option>
                    <option value="size-desc" className="bg-[#161722] text-white">Size (Largest)</option>
                    <option value="size-asc" className="bg-[#161722] text-white">Size (Smallest)</option>
                  </select>
                </div>
              </div>
            </div>

            {/* Grouped Titles Grid */}
            {loadingPackages ? (
              <div className="py-24 text-center">
                <div className="ps5-robust-spinner mx-auto" />
                <p className="text-sm text-zinc-400 mt-3">Loading packages from {selectedDrive.label}...</p>
              </div>
            ) : groupedTitles.length === 0 ? (
              <div className="py-16 text-center rounded-[2px] border border-white/10 bg-[#12131a]/40 p-8">
                <h3 className="text-lg font-bold text-white">No Matching Packages</h3>
                <p className="text-sm text-zinc-400 max-w-md mx-auto mt-2">
                  No packages matched your search query on {selectedDrive.label}.
                </p>
              </div>
            ) : (
              <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-5">
                {groupedTitles.map((group, index) => {
                  const isBaseInstalled = group.isBaseInstalled;
                  const hasBaseOnDrive = group.hasBaseOnDrive;
                  const hasNewBase = group.hasNewBase;
                  const isLatestUpdateInstalled = group.isLatestUpdateInstalled;
                  const hasNewUpdate = group.hasNewUpdate;
                  const areAllDlcsInstalled = group.areAllDlcsInstalled;
                  const hasNewDlc = group.hasNewDlc;
                  const isEverythingInstalled = group.isEverythingInstalled;

                  return (
                    <div
                      key={group.id}
                      role="button"
                      tabIndex={0}
                      onClick={() => handleOpenTitle(group.id)}
                      onKeyDown={(e) => {
                        if (e.key === 'Enter' || e.key === ' ') {
                          e.preventDefault();
                          handleOpenTitle(group.id);
                        }
                      }}
                      className={`w-full group block text-left rounded-[2px] ps5-focus-item p-2.5 border transition-all cursor-pointer ${
                        (settings.fade_installed_packages && isEverythingInstalled)
                          ? 'card-darked-out'
                          : 'bg-[#141520] hover:bg-[#171824] border-white/10 hover:border-white/20'
                      }`}
                    >
                      {/* Square Image Box (with fallback for Safari <15) */}
                      <div className="aspect-square-box rounded-[2px] overflow-hidden bg-black/50 border border-white/10">
                        <div className="aspect-square-content overflow-hidden">
                          {/* Fallback Icon */}
                          <div className="absolute inset-0 flex items-center justify-center text-zinc-600 pointer-events-none">
                            <svg className="w-12 h-12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                              <rect x="2" y="3" width="20" height="14" rx="2" />
                              <line x1="8" y1="21" x2="16" y2="21" />
                              <line x1="12" y1="17" x2="12" y2="21" />
                            </svg>
                          </div>

                          {/* Package Image */}
                          {group.iconPath ? (
                            <BlurIcon
                              pkg={group.imagePkg}
                              alt={group.title_name}
                              priority={index < 10}
                              imgClassName="absolute inset-0 w-full h-full object-cover z-10 block"
                            />
                          ) : null}

                          {/* Installed badge on top-left if base is installed */}
                          {group.isBaseInstalled && (
                            <span className="absolute top-2 left-2 z-20 px-2 py-0.5 rounded-[2px] bg-emerald-600/90 text-[10px] font-bold text-white border border-emerald-400/30">
                              INSTALLED
                            </span>
                          )}

                          {/* Leftover badge on top-left if base is missing but leftovers exist */}
                          {!group.isBaseInstalled && group.hasLeftover && (
                            <span
                              className="absolute top-2 left-2 z-20 px-2 py-0.5 rounded-[2px] bg-amber-600/95 text-[10px] font-bold text-white border border-amber-400/40 flex items-center space-x-1"
                              title={group.leftoverDesc || 'Leftover update or DLC exists on console without base package'}
                            >
                              <svg className="w-3 h-3 text-amber-200" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                                <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                              </svg>
                              <span>LEFTOVER</span>
                            </span>
                          )}

                          {/* Aborted / Partially installed badge on top-left if install was aborted */}
                          {!group.isBaseInstalled && !group.hasLeftover && group.isPartiallyInstalled && (
                            <span
                              className="absolute top-2 left-2 z-20 px-2 py-0.5 rounded-[2px] bg-rose-600/95 text-[10px] font-bold text-white border border-rose-400/40 flex items-center space-x-1"
                              title={group.partialDesc || 'Partially installed or aborted installation detected on console'}
                            >
                              <svg className="w-3 h-3 text-rose-200" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                                <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                              </svg>
                              <span>ABORTED</span>
                            </span>
                          )}

                          {/* Source tag on bottom-left of square image (only in All Sources mode) */}
                          {selectedDrive?.id === '__all__' && group.sourceName && (
                            <div className="absolute bottom-2 left-2 z-20 pointer-events-none max-w-[45%]">
                              <span className={`text-[10px] font-bold px-1.5 py-0.5 rounded-[2px] border truncate block ${
                                group.sourceType === 'smb'
                                  ? 'bg-cyan-950/85 text-cyan-300 border-cyan-500/50'
                                  : group.sourceType === 'disc'
                                  ? 'bg-purple-950/85 text-purple-300 border-purple-500/50'
                                  : 'bg-zinc-800 text-zinc-300 border-white/20'
                              }`}>
                                {group.sourceName}
                              </span>
                            </div>
                          )}

                          {/* Tags on bottom-right of square image */}
                          <div className="absolute bottom-2 right-2 z-20 flex flex-col items-end space-y-1 pointer-events-none">
                            {group.base && (
                              <span className={`text-[10px] font-bold px-1.5 py-0.5 rounded-[2px] border ${
                                hasNewBase
                                  ? 'bg-blue-600 text-white border-blue-400/60'
                                  : 'bg-blue-950/70 text-blue-300 border-blue-800/40'
                              }`}>
                                base
                              </span>
                            )}
                            {group.updates.length > 0 && (
                              <span className={`text-[10px] font-mono px-1.5 py-0.5 rounded-[2px] border ${
                                hasNewUpdate
                                  ? 'bg-purple-600 text-white border-purple-300 font-bold'
                                  : 'bg-purple-950/70 text-purple-300/80 border-purple-800/40 font-medium'
                              }`}>
                                {hasNewUpdate ? 'NEW: ' : ''}update{group.latestUpdateVersion ? ` ${group.latestUpdateVersion}` : ''}
                              </span>
                            )}
                            {group.dlcCount > 0 && (
                              <span className={`text-[10px] px-1.5 py-0.5 rounded-[2px] border ${
                                hasNewDlc
                                  ? 'bg-emerald-600 text-white border-emerald-300 font-bold'
                                  : 'bg-emerald-950/70 text-emerald-300/80 border-emerald-800/40 font-medium'
                              }`}>
                                {hasNewDlc ? 'NEW: ' : ''}{group.dlcCount} {group.dlcCount === 1 ? 'DLC' : 'DLCs'}
                              </span>
                            )}
                          </div>
                        </div>
                      </div>

                      {/* Title Below Image */}
                      <h3
                        className="text-sm font-bold text-white truncate mt-2 w-full transition-colors"
                        title={group.title_name}
                      >
                        {group.title_name}
                      </h3>

                      {/* Subtitle / ID & Size */}
                      <div className="text-xs text-zinc-400 font-mono flex items-center justify-between mt-0.5 w-full">
                        <span>{group.title_id || 'PKG'}</span>
                        <span className="text-zinc-500">
                          {group.hasMultipart ? `${formatBytes(group.totalDriveSize)} / ${formatBytes(group.totalFullSize)}` : formatBytes(group.totalSize)}
                        </span>
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        )}

        {/* VIEW 3: Title Detail View */}
        {selectedDrive && selectedTitle && (
          <div className="max-w-6xl mx-auto px-4 sm:px-8 space-y-6 pb-12">
            {/* Back to Packages Button */}
            <div className="flex items-center space-x-4 border-b border-white/10 pb-4">
              <button
                type="button"
                onClick={handleBackToPackages}
                className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 border border-white/10 text-base font-semibold transition-all flex items-center space-x-2 text-zinc-200 shrink-0 cursor-pointer"
              >
                <span>&larr;</span>
                <span>Back to Packages</span>
              </button>

              <div className="text-sm text-zinc-400 flex items-center space-x-2 min-w-0">
                <span className="shrink-0">
                  {selectedDrive.id === '__all__'
                    ? (selectedTitle.sourceName ? `All Sources (${selectedTitle.sourceName})` : 'All Sources')
                    : selectedDrive.label}
                </span>
                <span className="shrink-0 text-zinc-600">&rsaquo;</span>
                <span className="text-white font-semibold truncate">{selectedTitle.title_name}</span>
              </div>
            </div>

            {/* Title Hero Card */}
            <div className="rounded-[2px] bg-[#141520] border border-white/10 p-7 flex flex-col space-y-6">
              <div className="flex flex-col md:flex-row items-start md:items-center justify-between space-y-6 md:space-y-0 md:space-x-8 w-full">
                <div className="flex flex-col sm:flex-row items-start sm:items-center space-y-4 sm:space-y-0 sm:space-x-7 min-w-0 flex-1">
                  {/* Large Base / Representative Image */}
                  <div className="w-40 h-40 sm:w-48 sm:h-48 rounded-[2px] overflow-hidden bg-black/50 border border-white/10 shrink-0 relative">
                    <div className="absolute inset-0 flex items-center justify-center text-zinc-600 pointer-events-none">
                      <svg className="w-14 h-14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                        <rect x="2" y="3" width="20" height="14" rx="2" />
                        <line x1="8" y1="21" x2="16" y2="21" />
                        <line x1="12" y1="17" x2="12" y2="21" />
                      </svg>
                    </div>
                    {selectedTitle.iconPath ? (
                      <BlurIcon
                        pkg={selectedTitle.imagePkg}
                        alt={selectedTitle.title_name}
                        priority={true}
                        imgClassName="absolute inset-0 w-full h-full object-cover z-10 block"
                      />
                    ) : null}
                  </div>

                  {/* Metadata */}
                  <div className="min-w-0 flex-1">
                    <h2 className="text-3xl sm:text-4xl font-black text-white leading-tight">
                      {selectedTitle.title_name}
                    </h2>

                    <div className="flex items-center space-x-2.5 mt-3 flex-wrap text-xs sm:text-sm">
                      {selectedTitle.title_id ? (
                        <span className="font-mono px-3 py-1 my-0.5 rounded-[2px] bg-white/5 text-zinc-200 border border-white/10 shrink-0 font-medium">
                          {selectedTitle.title_id}
                        </span>
                      ) : null}

                      {selectedDrive.id === '__all__' && selectedTitle.sourceName && (
                        <span className={`px-3 py-1 my-0.5 rounded-[2px] font-bold border shrink-0 text-xs sm:text-sm ${
                          selectedTitle.sourceType === 'smb'
                            ? 'bg-cyan-500/20 text-cyan-300 border-cyan-500/30'
                            : selectedTitle.sourceType === 'disc'
                            ? 'bg-purple-500/20 text-purple-300 border-purple-500/30'
                            : 'bg-blue-500/20 text-blue-300 border-blue-500/30'
                        }`}>
                          {selectedTitle.sourceName}
                        </span>
                      )}

                      {selectedTitle.hasMultipart ? (
                        <>
                          <span className="font-mono px-3 py-1 my-0.5 rounded-[2px] bg-white/5 text-zinc-200 border border-white/10 shrink-0 font-medium">
                            Size: {formatBytes(selectedTitle.totalDriveSize)} (Drive) / {formatBytes(selectedTitle.totalFullSize)} Full
                          </span>
                          {selectedTitle.isBaseMultipart && (
                            <span className="px-3 py-1 my-0.5 rounded-[2px] bg-blue-500/20 text-blue-300 border border-blue-500/30 font-bold shrink-0">
                              Base {selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of {selectedTitle.totalParts}
                            </span>
                          )}
                        </>
                      ) : (
                        <span className="font-mono px-3 py-1 my-0.5 rounded-[2px] bg-white/5 text-zinc-200 border border-white/10 shrink-0 font-medium">
                          Total: {formatBytes(selectedTitle.totalSize)}
                        </span>
                      )}

                      {selectedTitle.isBaseInstalled ? (
                        <span className="px-3 py-1 my-0.5 rounded-[2px] bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 font-bold shrink-0">
                          Base Installed{selectedTitle.installedVersion ? ` (${selectedTitle.installedVersion})` : ''}
                        </span>
                      ) : selectedTitle.hasLeftover ? (
                        <span className="px-3 py-1 my-0.5 rounded-[2px] bg-amber-500/20 text-amber-300 border border-amber-500/30 font-bold shrink-0 flex items-center space-x-1.5">
                          <svg className="w-3.5 h-3.5 text-amber-300" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                            <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                          </svg>
                          <span>Leftovers Found</span>
                        </span>
                      ) : null}
                    </div>

                    {(!selectedTitle.isBaseInstalled && !selectedTitle.base) ? (
                      <p className="text-xs text-amber-300 mt-3 font-medium">
                        Base package is required to install updates and DLC.
                      </p>
                    ) : (selectedTitle.base && selectedTitle.isMultipart && (!selectedTitle.base.is_installed || selectedTitle.base.can_install !== false)) ? (
                      <p className="text-xs text-zinc-400 mt-3">
                        Subsequent {selectedTitle.sourceType === 'disc' ? 'discs' : 'parts'} will be requested during installation.
                      </p>
                    ) : null}
                  </div>
                </div>

                {/* Base Package Action Area */}
                <div className="shrink-0 w-full sm:w-auto">
                  {selectedTitle.base ? (
                    (selectedTitle.base.is_installed && selectedTitle.base.can_install === false) ? (
                      <div className="px-6 py-3 rounded-[2px] bg-emerald-500/15 border border-emerald-500/30 text-emerald-300 text-base font-bold flex items-center justify-center space-x-2 whitespace-nowrap">
                        <svg className="w-5 h-5 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                          <polyline points="20 6 9 17 4 12" />
                        </svg>
                        <span>Base Installed</span>
                      </div>
                    ) : (
                      <div className="flex flex-col items-stretch space-y-2.5">
                        {selectedTitle.updates.length > 0 && (
                          <button
                            type="button"
                            onClick={() => handleInstallBaseAndUpdate(selectedTitle.base, selectedTitle.updates[0])}
                            disabled={installerStatus.is_installing || selectedTitle.hasLeftover || selectedTitle.base.can_install === false}
                            title={selectedTitle.hasLeftover ? 'Leftovers detected on console. Clean up leftovers before installing.' : (selectedTitle.base.install_disabled_reason || '')}
                            className={`w-full px-6 py-3.5 rounded-[2px] ps5-focus-item font-bold text-base transition-all flex items-center justify-center space-x-2.5 whitespace-nowrap ${
                              (selectedTitle.hasLeftover || selectedTitle.base.can_install === false)
                                ? 'bg-zinc-800/80 text-zinc-500 border border-white/5 cursor-not-allowed'
                                : 'bg-gradient-to-r from-blue-600 to-purple-600 hover:from-blue-500 hover:to-purple-500 text-white disabled:opacity-50 cursor-pointer'
                            }`}
                          >
                            <svg className="w-5 h-5 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                              <polyline points="7 10 12 15 17 10" />
                              <line x1="12" y1="15" x2="12" y2="3" />
                            </svg>
                            <span>
                              {selectedTitle.base.is_installed ? 'Upgrade ' : selectedTitle.isPartiallyInstalled ? 'Reinstall ' : 'Install '}
                              {selectedTitle.isMultipart ? `${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${selectedTitle.totalParts} + Update` : 'Base + Update'}
                              {' '}(
                              {selectedTitle.isMultipart
                                ? `${formatBytes(selectedTitle.firstPartSize + selectedTitle.updates[0].file_size)} / ${formatBytes(selectedTitle.baseFullSize + selectedTitle.updates[0].file_size)}`
                                : formatBytes(selectedTitle.base.file_size + selectedTitle.updates[0].file_size)}
                              )
                            </span>
                          </button>
                        )}
                        <button
                          type="button"
                          onClick={() => handleInstall(selectedTitle.base)}
                          disabled={installerStatus.is_installing || selectedTitle.hasLeftover || selectedTitle.base.can_install === false}
                          title={selectedTitle.hasLeftover ? 'Leftovers detected on console. Clean up leftovers before installing.' : (selectedTitle.base.install_disabled_reason || '')}
                          className={`w-full px-5 py-3.5 rounded-[2px] ps5-focus-item font-bold text-base transition-all flex items-center justify-center space-x-2.5 whitespace-nowrap ${
                            (selectedTitle.hasLeftover || selectedTitle.base.can_install === false)
                              ? 'bg-zinc-800/80 text-zinc-500 border border-white/5 cursor-not-allowed'
                              : 'bg-blue-600/80 hover:bg-blue-600 text-white disabled:opacity-50 cursor-pointer border border-white/10'
                          }`}
                        >
                          <svg className="w-5 h-5 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                            <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                            <polyline points="7 10 12 15 17 10" />
                            <line x1="12" y1="15" x2="12" y2="3" />
                          </svg>
                          <span>
                            {selectedTitle.base.is_installed ? 'Upgrade ' : selectedTitle.isPartiallyInstalled ? 'Reinstall ' : 'Install '}
                            {selectedTitle.isMultipart
                              ? `${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${selectedTitle.totalParts}${selectedTitle.updates.length > 0 ? ' Only' : ''}`
                              : (selectedTitle.updates.length > 0
                                  ? `Base ${formatVersion(selectedTitle.base?.app_version) || 'v1.00'} Only`
                                  : `Base ${formatVersion(selectedTitle.base?.app_version) || 'v1.00'}`)}
                            {' '}(
                            {selectedTitle.isMultipart
                              ? `${formatBytes(selectedTitle.firstPartSize)} / ${formatBytes(selectedTitle.baseFullSize)}`
                              : formatBytes(selectedTitle.base.file_size)}
                            )
                          </span>
                        </button>
                      </div>
                    )
                  ) : (
                    <div className="px-5 py-3 rounded-[2px] bg-white/5 border border-white/10 text-sm font-medium text-zinc-400 text-center whitespace-nowrap">
                      No Base PKG on Drive
                    </div>
                  )}
                </div>
              </div>

              {/* Leftover Alert Banner in Detail View */}
              {!selectedTitle.isBaseInstalled && selectedTitle.hasLeftover && (
                <div className="w-full pt-4 border-t border-white/10">
                  <div className="p-4 sm:p-5 rounded-[2px] bg-amber-500/15 border border-amber-500/30 text-amber-200 text-xs sm:text-sm flex flex-col sm:flex-row sm:items-center justify-between gap-4">
                    <div className="flex items-start space-x-3.5 min-w-0 flex-1">
                      <svg className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                        <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                      </svg>
                      <div className="space-y-1">
                        <div className="font-bold text-amber-300 text-sm sm:text-base">Leftovers Detected on Console</div>
                        <div className="text-zinc-300 leading-relaxed">
                          {selectedTitle.leftoverDesc ? `${selectedTitle.leftoverDesc}. ` : 'Files from a previous installation were found without a base package. '}
                          Clean up leftovers before installing.
                        </div>
                      </div>
                    </div>
                    <button
                      type="button"
                      onClick={() => handleOpenLeftoverCleanupForTitle(selectedTitle.title_id, selectedTitle.title_name)}
                      className="shrink-0 px-5 py-2.5 rounded-[2px] ps5-focus-item bg-amber-500 hover:bg-amber-400 text-black font-bold text-xs sm:text-sm transition-colors flex items-center justify-center space-x-2 cursor-pointer whitespace-nowrap"
                    >
                      <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                        <polyline points="3 6 5 6 21 6" />
                        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                      </svg>
                      <span>Clean Up Leftovers</span>
                    </button>
                  </div>
                </div>
              )}

              {/* Partially Installed / Aborted Alert Banner in Detail View */}
              {!selectedTitle.isBaseInstalled && !selectedTitle.hasLeftover && selectedTitle.isPartiallyInstalled && (
                <div className="w-full pt-4 border-t border-white/10">
                  <div className="p-4 sm:p-5 rounded-[2px] bg-rose-500/15 border border-rose-500/30 text-rose-200 text-xs sm:text-sm flex flex-col sm:flex-row sm:items-center justify-between gap-4">
                    <div className="flex items-start space-x-3.5 min-w-0 flex-1">
                      <svg className="w-5 h-5 text-rose-400 shrink-0 mt-0.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                        <path strokeLinecap="round" strokeLinejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                      </svg>
                      <div className="space-y-1">
                        <div className="font-bold text-rose-300 text-sm sm:text-base">Incomplete Installation Detected</div>
                        <div className="text-zinc-300 leading-relaxed">
                          An aborted installation was detected on the console. Delete the broken icon from your PS5 home screen (Options &rarr; Delete) or reinstall the base package.
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              )}
            </div>

            {/* Updates Section */}
            {selectedTitle.updates.length > 0 && (
              <div className="space-y-3">
                <h3 className="text-xl font-bold text-white flex items-center space-x-2.5">
                  <span>Updates</span>
                  <span className="text-xs px-2.5 py-1 rounded-[2px] bg-purple-500/20 text-purple-300 border border-purple-500/30 font-bold">
                    {selectedTitle.updates.length} Available
                  </span>
                </h3>

                <div className="space-y-2.5">
                  {selectedTitle.updates.map((pkg) => {
                    const isUpdMultipart = !!pkg.is_multipart && (Number(pkg.total_parts) > 1);
                    const updTotalParts = Number(pkg.total_parts) || 1;
                    const updFullSize = Number(pkg.total_pkg_size || pkg.file_size) || 0;
                    const requiredSpace = updFullSize;
                    const notEnoughSpace = storage && storage.free && storage.free < requiredSpace;
                    const canInstall = pkg.can_install !== false;
                    const isInstallDisabled = !canInstall || notEnoughSpace || installerStatus.is_installing;

                    let disabledLabel = 'Unavailable';
                    if (selectedTitle.hasLeftover || (pkg.install_disabled_reason && pkg.install_disabled_reason.includes('Leftovers detected'))) {
                      disabledLabel = 'Leftovers Found';
                    } else if (pkg.install_disabled_reason && pkg.install_disabled_reason.includes('aborted')) {
                      disabledLabel = 'Base Aborted';
                    } else if (pkg.install_disabled_reason && (pkg.install_disabled_reason.includes('Base package is not installed') || pkg.install_disabled_reason.includes('Base game is not installed'))) {
                      disabledLabel = 'Base Required';
                    } else if (pkg.install_disabled_reason === 'Installed version is same or newer') {
                      disabledLabel = 'Up to Date';
                    } else if (!pkg.is_installed) {
                      disabledLabel = 'Base Required';
                    } else if (pkg.can_install === false) {
                      disabledLabel = 'Up to Date';
                    }

                    return (
                      <div
                        key={pkg.path}
                        className="rounded-[2px] p-5 bg-[#141520] border border-white/10 flex items-center justify-between space-x-5"
                      >
                        <div className="flex items-center space-x-5 min-w-0 flex-1">
                          <div className="w-14 h-14 rounded-[2px] overflow-hidden bg-black/50 border border-purple-500/30 shrink-0 flex items-center justify-center relative">
                            {pkg.has_icon ? (
                              <BlurIcon
                                pkg={pkg}
                                alt={pkg.title_name || 'Update'}
                                priority={true}
                                imgClassName="absolute inset-0 w-full h-full object-cover z-10 block"
                              />
                            ) : null}
                            <div className="w-full h-full bg-purple-950/40 flex items-center justify-center text-purple-300 font-black text-xs tracking-wider">
                              UPDATE
                            </div>
                          </div>

                          <div className="min-w-0 flex-1">
                            <div className="flex items-center space-x-2.5 flex-wrap gap-y-1">
                              <span className="text-base font-bold text-white">
                                {pkg.app_version ? `Update ${formatVersion(pkg.app_version)}` : 'Package Update'}
                              </span>
                              {isUpdMultipart ? (
                                <>
                                  <span className="text-xs sm:text-sm font-mono text-zinc-300 shrink-0 font-medium">
                                    ({formatBytes(pkg.file_size)} / {formatBytes(updFullSize)})
                                  </span>
                                  <span className="text-[10px] font-bold px-2 py-0.5 rounded-[2px] bg-purple-500/20 text-purple-300 border border-purple-500/30 shrink-0">
                                    {selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of {updTotalParts}
                                  </span>
                                </>
                              ) : (
                                <span className="text-xs sm:text-sm font-mono text-zinc-400 shrink-0">
                                  ({formatBytes(pkg.file_size)})
                                </span>
                              )}
                            </div>

                            {isUpdMultipart && (
                              <p className="text-xs text-purple-300/80 mt-1">
                                Subsequent {selectedTitle.sourceType === 'disc' ? 'discs' : 'parts'} will be requested during installation.
                              </p>
                            )}

                            {!canInstall && pkg.install_disabled_reason ? (
                              <p className="text-xs text-amber-400 mt-1">
                                • {pkg.install_disabled_reason}
                              </p>
                            ) : null}
                          </div>
                        </div>

                        <div className="shrink-0">
                          <button
                            type="button"
                            onClick={() => !isInstallDisabled && handleInstall(pkg)}
                            disabled={isInstallDisabled}
                            className={`px-5 py-2.5 rounded-[2px] ps5-focus-item text-sm font-bold transition-all whitespace-nowrap ${
                              isInstallDisabled
                                ? 'bg-zinc-800 text-zinc-500 border border-white/5 cursor-not-allowed'
                                : 'bg-purple-600 hover:bg-purple-500 text-white cursor-pointer'
                            }`}
                          >
                            {notEnoughSpace
                              ? 'No Space'
                              : !canInstall
                              ? disabledLabel
                              : isUpdMultipart
                              ? `Install ${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${updTotalParts}`
                              : 'Install Update'}
                          </button>
                        </div>
                      </div>
                    );
                  })}
                </div>
              </div>
            )}

            {/* DLCs Section */}
            {selectedTitle.dlcs.length > 0 && (
              <div className="space-y-3">
                <h3 className="text-xl font-bold text-white flex items-center space-x-2.5">
                  <span>Downloadable Content (DLC)</span>
                  <span className="text-xs px-2.5 py-1 rounded-[2px] bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 font-bold">
                    {selectedTitle.dlcs.length} Available
                  </span>
                </h3>

                <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-5">
                  {selectedTitle.dlcs.map((pkg, index) => {
                    const isDlcMultipart = !!pkg.is_multipart && (Number(pkg.total_parts) > 1);
                    const dlcTotalParts = Number(pkg.total_parts) || 1;
                    const dlcFullSize = Number(pkg.total_pkg_size || pkg.file_size) || 0;
                    const requiredSpace = dlcFullSize;
                    const notEnoughSpace = storage && storage.free && storage.free < requiredSpace;
                    const canInstall = pkg.can_install !== false;
                    const isInstallDisabled = !canInstall || notEnoughSpace || installerStatus.is_installing;

                    let disabledLabel = 'Unavailable';
                    if (selectedTitle.hasLeftover || (pkg.install_disabled_reason && pkg.install_disabled_reason.includes('Leftovers detected'))) {
                      disabledLabel = 'Leftovers Found';
                    } else if (pkg.install_disabled_reason && pkg.install_disabled_reason.includes('aborted')) {
                      disabledLabel = 'Base Aborted';
                    } else if (
                      pkg.install_disabled_reason === 'Base package is not installed' ||
                      pkg.install_disabled_reason === 'Base game is not installed' ||
                      (pkg.install_disabled_reason && (pkg.install_disabled_reason.includes('Base package is not installed') || pkg.install_disabled_reason.includes('Base game is not installed'))) ||
                      !pkg.is_installed
                    ) {
                      disabledLabel = 'Base Required';
                    } else if (pkg.is_dlc_installed || pkg.install_disabled_reason === 'DLC is already installed') {
                      disabledLabel = 'Installed';
                    }

                    return (
                      <div
                        key={pkg.path}
                        onClick={(e) => {
                          if (!isInstallDisabled && e.target.tagName !== 'BUTTON') {
                            handleInstall(pkg);
                          }
                        }}
                        className={`w-full group flex flex-col justify-between text-left rounded-[2px] p-2.5 border transition-all ${
                          isInstallDisabled ? '' : 'cursor-pointer ps5-focus-item'
                        } ${
                          (settings.fade_installed_packages && pkg.is_dlc_installed)
                            ? 'card-darked-out'
                            : 'bg-[#141520] hover:bg-[#171824] border-white/10 hover:border-white/20'
                        }`}
                      >
                        <div>
                          {/* Square Image Box (with fallback for Safari <15) */}
                          <div className="aspect-square-box rounded-[2px] overflow-hidden bg-black/50 border border-white/10">
                            <div className="aspect-square-content overflow-hidden">
                              {/* Fallback Icon */}
                              <div className="absolute inset-0 flex items-center justify-center text-zinc-600 pointer-events-none">
                                <svg className="w-12 h-12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                                  <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
                                </svg>
                              </div>

                              {/* DLC Image */}
                              {pkg.has_icon ? (
                                <BlurIcon
                                  pkg={pkg}
                                  alt={pkg.title_name || 'DLC'}
                                  priority={index < 8}
                                  imgClassName="absolute inset-0 w-full h-full object-cover z-10 block"
                                />
                              ) : null}

                              {/* Installed badge on top-left if DLC is installed */}
                              {pkg.is_dlc_installed && (
                                <span className="absolute top-2 left-2 z-20 px-2 py-0.5 rounded-[2px] bg-emerald-600/90 text-[10px] font-bold text-white border border-emerald-400/30">
                                  INSTALLED
                                </span>
                              )}

                              {/* Tag on bottom-right of square image */}
                              <div className="absolute bottom-2 right-2 z-20 flex flex-col items-end space-y-1 pointer-events-none">
                                <span className="text-[10px] font-bold px-1.5 py-0.5 rounded-[2px] border bg-emerald-950/70 text-emerald-300/80 border-emerald-800/40">
                                  {isDlcMultipart ? `${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${dlcTotalParts}` : 'DLC'}
                                </span>
                              </div>
                            </div>
                          </div>

                          {/* Title Below Image */}
                          <h4
                            className="text-sm font-bold text-white truncate mt-2 w-full transition-colors"
                            title={pkg.title_name || 'DLC'}
                          >
                            {pkg.title_name || 'DLC'}
                          </h4>

                          {/* Size */}
                          <div className="text-xs text-zinc-400 font-mono flex items-center justify-between mt-0.5 w-full">
                            <span>DLC</span>
                            <span className="text-zinc-500">
                              {isDlcMultipart ? `${formatBytes(pkg.file_size)} / ${formatBytes(dlcFullSize)}` : formatBytes(pkg.file_size)}
                            </span>
                          </div>

                          {/* Warning (if any) */}
                          {!canInstall && pkg.install_disabled_reason ? (
                            <p className="text-[11px] text-amber-400 mt-1 truncate" title={pkg.install_disabled_reason}>
                              • {pkg.install_disabled_reason}
                            </p>
                          ) : null}
                        </div>

                        {/* Action Button */}
                        <div className="mt-3">
                          <button
                            type="button"
                            onClick={(e) => {
                              e.stopPropagation();
                              if (!isInstallDisabled) handleInstall(pkg);
                            }}
                            disabled={isInstallDisabled}
                            className={`w-full py-2 px-2 rounded-[2px] ps5-focus-item text-xs sm:text-sm font-bold transition-all text-center truncate ${
                              isInstallDisabled
                                ? 'bg-zinc-800 text-zinc-500 border border-white/5 cursor-not-allowed'
                                : 'bg-emerald-600 hover:bg-emerald-500 text-white cursor-pointer'
                            }`}
                          >
                            {notEnoughSpace ? 'No Space' : !canInstall ? disabledLabel : isDlcMultipart ? `Install ${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${dlcTotalParts}` : 'Install DLC'}
                          </button>
                        </div>
                      </div>
                    );
                  })}
                </div>
              </div>
            )}

            {/* Other Packages (if any) */}
            {selectedTitle.others.length > 0 && (
              <div className="space-y-3">
                <h3 className="text-xl font-bold text-white flex items-center space-x-2.5">
                  <span>Other Packages</span>
                  <span className="text-xs px-2.5 py-1 rounded-[2px] bg-white/10 text-zinc-300 border border-white/20 font-bold">
                    {selectedTitle.others.length}
                  </span>
                </h3>

                <div className="space-y-2.5">
                  {selectedTitle.others.map((pkg, index) => {
                    const isOtherMultipart = !!pkg.is_multipart && (Number(pkg.total_parts) > 1);
                    const otherTotalParts = Number(pkg.total_parts) || 1;
                    const otherFullSize = Number(pkg.total_pkg_size || pkg.file_size) || 0;
                    const requiredSpace = otherFullSize;
                    const notEnoughSpace = storage && storage.free && storage.free < requiredSpace;
                    const canInstall = pkg.can_install !== false;
                    const isInstallDisabled = !canInstall || notEnoughSpace || installerStatus.is_installing;

                    return (
                      <div
                        key={pkg.path}
                        className="rounded-[2px] p-5 bg-[#141520] border border-white/10 flex items-center justify-between space-x-5"
                      >
                        <div className="flex items-center space-x-5 min-w-0 flex-1">
                          <div className="w-14 h-14 rounded-[2px] overflow-hidden bg-black/50 border border-white/10 shrink-0 flex items-center justify-center relative">
                            {pkg.has_icon ? (
                              <BlurIcon
                                pkg={pkg}
                                alt={pkg.title_name || pkg.filename}
                                priority={index < 8}
                                imgClassName="absolute inset-0 w-full h-full object-cover z-10 block"
                              />
                            ) : null}
                            <div className="w-full h-full bg-white/5 flex items-center justify-center text-zinc-400 font-black text-xs tracking-wider">
                              PKG
                            </div>
                          </div>

                          <div className="min-w-0 flex-1">
                            <div className="flex items-center space-x-2.5 flex-wrap gap-y-1">
                              <span className="text-base font-bold text-white truncate" title={pkg.title_name || pkg.filename}>
                                {pkg.title_name || pkg.filename}
                              </span>
                              {isOtherMultipart && (
                                <span className="text-[10px] font-bold px-2 py-0.5 rounded-[2px] bg-white/10 text-zinc-300 border border-white/20 shrink-0">
                                  {selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of {otherTotalParts}
                                </span>
                              )}
                            </div>
                            <p className="text-xs sm:text-sm font-mono text-zinc-500 truncate mt-0.5">
                              {pkg.content_id || pkg.filename} • {isOtherMultipart ? `${formatBytes(pkg.file_size)} / ${formatBytes(otherFullSize)}` : formatBytes(pkg.file_size)}
                            </p>
                            {isOtherMultipart && (
                              <p className="text-xs text-zinc-400 mt-1">
                                Subsequent {selectedTitle.sourceType === 'disc' ? 'discs' : 'parts'} will be requested during installation.
                              </p>
                            )}
                          </div>
                        </div>

                        <div className="shrink-0">
                          <button
                            type="button"
                            onClick={() => !isInstallDisabled && handleInstall(pkg)}
                            disabled={isInstallDisabled}
                            className={`px-5 py-2.5 rounded-[2px] ps5-focus-item text-sm font-bold transition-all whitespace-nowrap ${
                              isInstallDisabled
                                ? 'bg-zinc-800 text-zinc-500 border border-white/5 cursor-not-allowed'
                                : 'bg-blue-600 hover:bg-blue-500 text-white cursor-pointer'
                            }`}
                          >
                            {notEnoughSpace ? 'No Space' : isOtherMultipart ? `Install ${selectedTitle.sourceType === 'disc' ? 'Disc' : 'Part'} 1 of ${otherTotalParts}` : 'Install'}
                          </button>
                        </div>
                      </div>
                    );
                  })}
                </div>
              </div>
            )}
          </div>
        )}
          </>
        )}
      </main>

      {/* Footer */}
      <footer className="w-full py-4 px-4 text-center text-[11px] text-zinc-600 border-t border-white/5 select-none">
        <div className="flex flex-wrap items-center justify-center gap-x-3 gap-y-1">
          <span>{getFullVersion(appVersion)}</span>
          <span className="text-zinc-700 hidden sm:inline">&bull;</span>
          {isPlayStation ? (
            <span>This project is free and open source: github.com/itsPLK/ps5-pkg-manager</span>
          ) : (
            <span>
              This project is free and open source:{' '}
              <a
                href="https://github.com/itsPLK/ps5-pkg-manager"
                target="_blank"
                rel="noopener noreferrer"
                className="text-zinc-500 hover:text-zinc-400 underline underline-offset-2 transition-colors"
              >
                GitHub
              </a>
            </span>
          )}
        </div>
      </footer>

      {/* Donation Popup Modal */}
      {showDonateModal && (
        <div className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
          <div className="bg-[#181a27] border border-white/10 rounded-[2px] max-w-md w-full p-6 space-y-5 shadow-2xl">
            {donateNeverNotice ? (
              <div className="py-8 text-center space-y-3">
                <div className="w-12 h-12 rounded-[2px] bg-emerald-500/20 border border-emerald-500/40 text-emerald-400 flex items-center justify-center mx-auto">
                  <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                    <polyline points="20 6 9 17 4 12" />
                  </svg>
                </div>
                <h4 className="text-base font-bold text-white">This popup won't be shown again</h4>
                <p className="text-xs text-zinc-400">Thank you for using PKG Manager!</p>
              </div>
            ) : (
              <>
                <div className="flex items-center justify-between pb-3 border-b border-white/10">
                  <div className="flex items-center space-x-3">
                    <div className="w-10 h-10 rounded-[2px] bg-rose-600/20 border border-rose-500/30 flex items-center justify-center text-rose-400 shrink-0">
                      <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                        <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z" />
                      </svg>
                    </div>
                    <div>
                      <h3 className="text-lg font-bold text-white">Support PKG Manager</h3>
                      <p className="text-xs text-zinc-400">Free &amp; Open-Source Software</p>
                    </div>
                  </div>
                  <button
                    type="button"
                    onClick={handleCloseDonateModal}
                    className="w-8 h-8 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-400 hover:text-white flex items-center justify-center transition-colors cursor-pointer"
                  >
                    ✕
                  </button>
                </div>

                <p className="text-xs text-zinc-300 leading-relaxed">
                  PKG Manager is free and open-source software. If you enjoy using it, please consider supporting its development!
                </p>

                {isPlayStation ? (
                  <div className="bg-black/40 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                    <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                      <QRCodeSVG
                        value={DONATE_URL}
                        size={150}
                        level="M"
                      />
                    </div>
                    <div className="text-center space-y-1">
                      <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                      <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                        github.com/itsPLK/ps5-pkg-manager
                      </span>
                    </div>
                  </div>
                ) : (
                  <div className="space-y-3">
                    <a
                      href={DONATE_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                      onClick={handleCloseDonateModal}
                      className="w-full py-3 px-4 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-sm font-bold transition-colors flex items-center justify-center cursor-pointer"
                    >
                      View donation options
                    </a>
                    <div className="flex justify-center">
                      <button
                        type="button"
                        onClick={() => setShowModalQr((prev) => !prev)}
                        className="text-[11px] text-zinc-400 hover:text-zinc-200 transition-colors cursor-pointer underline underline-offset-2"
                      >
                        {showModalQr ? 'Hide QR Code' : 'Show QR Code for phone scan'}
                      </button>
                    </div>
                    {showModalQr && (
                      <div className="bg-black/40 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                        <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                          <QRCodeSVG
                            value={DONATE_URL}
                            size={140}
                            level="M"
                          />
                        </div>
                        <div className="text-center space-y-1">
                          <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                          <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                            github.com/itsPLK/ps5-pkg-manager
                          </span>
                        </div>
                      </div>
                    )}
                  </div>
                )}

                <div className="flex items-center justify-between pt-2 border-t border-white/10">
                  <button
                    type="button"
                    onClick={handleNeverShowDonateModal}
                    className="text-xs text-zinc-500 hover:text-zinc-300 ps5-focus-item px-2.5 py-1.5 rounded-[2px] transition-colors cursor-pointer"
                  >
                    Don't show again
                  </button>
                  <button
                    type="button"
                    onClick={handleCloseDonateModal}
                    className="px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-200 text-xs font-semibold transition-colors cursor-pointer"
                  >
                    Maybe Later
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}

      {/* Clear Cache Confirmation Modal */}
      {showClearCacheModal && (
        <div className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
          <div className="bg-[#181a27] border border-rose-500/30 rounded-[2px] max-w-md w-full p-6 space-y-5">
            <div className="flex items-center space-x-3">
              <div className="w-10 h-10 rounded-[2px] bg-rose-600/20 border border-rose-500/40 flex items-center justify-center text-rose-400 shrink-0">
                <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <path d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                </svg>
              </div>
              <div>
                <h3 className="text-lg font-bold text-white">Clear Package Cache?</h3>
                <p className="text-xs text-zinc-400">Remove cached metadata & icons from console storage</p>
              </div>
            </div>

            <p className="text-xs text-zinc-300 leading-relaxed">
              This will delete all cached metadata and icons stored at <span className="font-mono text-blue-300">{cacheStats?.cache_path || '/data/pkgmgr/cache'}</span>:
            </p>

            {cacheStats && cacheStats.total_count > 0 && (
              <div className="bg-black/40 border border-white/10 rounded-[2px] p-3 space-y-1.5 text-xs font-mono">
                <div className="flex items-center justify-between text-zinc-300">
                  <span>Packages Cached:</span>
                  <span className="text-white font-bold">{cacheStats.total_count}</span>
                </div>
                <div className="pt-2 border-t border-white/10 flex items-center justify-between font-bold text-white">
                  <span>Total Space Freed:</span>
                  <span className="text-rose-400">{formatBytes(cacheStats.total_bytes)}</span>
                </div>
              </div>
            )}

            <p className="text-[11px] text-zinc-400">
              Your original <span className="font-mono text-zinc-300">.pkg</span> files will not be deleted. The cache will automatically rebuild as you browse packages.
            </p>

            <div className="flex items-center justify-end space-x-3 pt-2">
              <button
                type="button"
                onClick={() => setShowClearCacheModal(false)}
                disabled={clearingCache}
                className="px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-300 text-xs font-semibold transition-colors cursor-pointer disabled:opacity-50"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={handleClearCache}
                disabled={clearingCache}
                className="px-4 py-2 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-xs font-bold transition-colors cursor-pointer disabled:opacity-50 flex items-center space-x-2"
              >
                {clearingCache ? (
                  <>
                    <div className="ps5-robust-spinner-sm" />
                    <span>Clearing...</span>
                  </>
                ) : (
                  <span>Clear Cache</span>
                )}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Add / Edit Samba Share Modal */}
      {showSmbModal && (
        <div className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
          <div className="bg-[#181a27] border border-white/15 rounded-[2px] max-w-lg w-full p-6 space-y-5 overflow-y-auto max-h-[90vh]">
            <div className="flex items-center justify-between pb-3 border-b border-white/10">
              <div className="flex items-center space-x-3">
                <div className="w-10 h-10 rounded-[2px] bg-cyan-600/20 border border-cyan-500/30 flex items-center justify-center text-cyan-400 shrink-0">
                  <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <rect x="2" y="2" width="20" height="8" rx="2" />
                    <rect x="2" y="14" width="20" height="8" rx="2" />
                    <line x1="6" y1="6" x2="6.01" y2="6" />
                    <line x1="6" y1="18" x2="6.01" y2="18" />
                    <path d="M12 10v4" />
                  </svg>
                </div>
                <div>
                  <h3 className="text-lg font-bold text-white">
                    {smbEditIndex >= 0 ? 'Edit Samba Share' : 'Add Samba Share'}
                  </h3>
                  <p className="text-xs text-zinc-400">Configure connection to your network storage</p>
                </div>
              </div>
              <button
                type="button"
                onClick={() => setShowSmbModal(false)}
                className="text-zinc-400 hover:text-white text-lg font-bold p-1 cursor-pointer ps5-focus-item rounded-[2px]"
              >
                &times;
              </button>
            </div>

            <div className="space-y-4 text-xs">
              {/* Label */}
              <div>
                <label className="block font-semibold text-zinc-300 mb-1">Friendly Label</label>
                <input
                  type="text"
                  placeholder="e.g. NAS Packages, PC Share"
                  value={smbForm.label}
                  onChange={(e) => setSmbForm({ ...smbForm, label: e.target.value })}
                  className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 focus:outline-none focus:border-white/40"
                />
              </div>

              {/* Server & Port */}
              <div className="grid grid-cols-3 gap-3">
                <div className="col-span-2">
                  <label className="block font-semibold text-zinc-300 mb-1">Server / IP Address *</label>
                  <input
                    type="text"
                    placeholder="192.168.1.100 or nas.local"
                    value={smbForm.server}
                    onChange={(e) => setSmbForm({ ...smbForm, server: e.target.value })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 font-mono focus:outline-none focus:border-white/40"
                  />
                </div>
                <div>
                  <label className="block font-semibold text-zinc-300 mb-1">Port</label>
                  <input
                    type="number"
                    placeholder="445"
                    value={smbForm.port ?? ''}
                    onChange={(e) => setSmbForm({ ...smbForm, port: e.target.value === '' ? '' : (parseInt(e.target.value, 10) || '') })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white font-mono focus:outline-none focus:border-white/40"
                  />
                </div>
              </div>

              {/* Share & Subfolder */}
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block font-semibold text-zinc-300 mb-1">Share Name *</label>
                  <input
                    type="text"
                    placeholder="e.g. pkgs, public"
                    value={smbForm.share}
                    onChange={(e) => setSmbForm({ ...smbForm, share: e.target.value })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 font-mono focus:outline-none focus:border-white/40"
                  />
                </div>
                <div>
                  <label className="block font-semibold text-zinc-300 mb-1">Subfolder (Optional)</label>
                  <input
                    type="text"
                    placeholder="e.g. ps5/pkgs"
                    value={smbForm.path}
                    onChange={(e) => setSmbForm({ ...smbForm, path: e.target.value })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 font-mono focus:outline-none focus:border-white/40"
                  />
                </div>
              </div>

              {/* Credentials */}
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block font-semibold text-zinc-300 mb-1">Username (Guest if blank)</label>
                  <input
                    type="text"
                    placeholder="anonymous"
                    value={smbForm.username}
                    onChange={(e) => setSmbForm({ ...smbForm, username: e.target.value })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 focus:outline-none focus:border-white/40"
                  />
                </div>
                <div>
                  <label className="block font-semibold text-zinc-300 mb-1">Password</label>
                  <input
                    type="password"
                    placeholder="••••••••"
                    value={smbForm.password}
                    onChange={(e) => setSmbForm({ ...smbForm, password: e.target.value })}
                    className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 focus:outline-none focus:border-white/40"
                  />
                </div>
              </div>

              {/* Workgroup */}
              <div>
                <label className="block font-semibold text-zinc-300 mb-1">Workgroup / Domain</label>
                <input
                  type="text"
                  placeholder="WORKGROUP"
                  value={smbForm.workgroup}
                  onChange={(e) => setSmbForm({ ...smbForm, workgroup: e.target.value })}
                  className="w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 focus:outline-none focus:border-white/40"
                />
              </div>

              {/* Read-Only Option */}
              <div
                onClick={() => setSmbForm({ ...smbForm, is_read_only: !smbForm.is_read_only })}
                className="bg-white/5 hover:bg-white/10 border border-white/10 rounded-[2px] ps5-focus-item p-3.5 flex items-center justify-between cursor-pointer transition-colors"
              >
                <div className="min-w-0 flex-1 mr-3">
                  <span className="font-semibold text-white block">Share is Read-Only</span>
                </div>
                <div className={`w-5 h-5 rounded-[2px] border flex items-center justify-center shrink-0 ${
                  smbForm.is_read_only ? 'bg-amber-600 border-amber-500 text-white' : 'bg-black/40 border-white/20'
                }`}>
                  {smbForm.is_read_only && (
                    <svg className="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3">
                      <polyline points="20 6 9 17 4 12" />
                    </svg>
                  )}
                </div>
              </div>

              {/* Test Result Banner */}
              {smbTestResult && (
                <div className={`rounded-[2px] p-3 border text-xs flex items-center space-x-2.5 ${
                  smbTestResult.success
                    ? 'bg-emerald-500/15 border-emerald-500/30 text-emerald-300'
                    : 'bg-rose-500/15 border-rose-500/30 text-rose-300'
                }`}>
                  <span className="font-bold">{smbTestResult.success ? '✓' : '✗'}</span>
                  <span className="flex-1">{smbTestResult.message || smbTestResult.error || (smbTestResult.success ? 'Connected successfully!' : 'Connection failed')}</span>
                </div>
              )}
            </div>

            {/* Buttons */}
            <div className="flex items-center justify-between pt-2 border-t border-white/10">
              <button
                type="button"
                onClick={() => handleTestSmbConnection(smbForm)}
                disabled={smbTesting || !smbForm.server?.trim() || (!smbForm.share?.trim() && !smbForm.server?.includes('/') && !smbForm.server?.includes('\\'))}
                className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-200 text-xs font-semibold transition-colors disabled:opacity-40 cursor-pointer flex items-center space-x-2"
              >
                {smbTesting && <div className="ps5-robust-spinner-sm" />}
                <span>{smbTesting ? 'Testing...' : 'Test Connection'}</span>
              </button>

              <div className="flex items-center space-x-3">
                <button
                  type="button"
                  onClick={() => setShowSmbModal(false)}
                  className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-400 hover:text-white text-xs font-semibold transition-colors cursor-pointer"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  onClick={handleSaveSmbShare}
                  disabled={!smbForm.server?.trim() || (!smbForm.share?.trim() && !smbForm.server?.includes('/') && !smbForm.server?.includes('\\'))}
                  className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-cyan-600 hover:bg-cyan-500 text-white text-xs font-bold transition-colors disabled:opacity-40 cursor-pointer"
                >
                  Save Share
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Orphaned Leftover Deletion Confirmation Modal */}
      {selectedLeftoverToDelete && (
        <div className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
          <div className="bg-[#181a27] border border-rose-500/30 rounded-[2px] max-w-lg w-full p-6 space-y-5">
            <div className="flex items-center space-x-3">
              <div className="w-12 h-12 rounded-[2px] bg-rose-600/20 border border-rose-500/40 flex items-center justify-center text-rose-400 shrink-0">
                <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <path d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                </svg>
              </div>
              <div className="min-w-0 flex-1">
                <h3 className="text-lg font-bold text-white truncate">
                  Delete Orphaned Leftovers?
                </h3>
                <p className="text-xs text-zinc-400">
                  {selectedLeftoverToDelete.title_name} ({selectedLeftoverToDelete.title_id})
                </p>
              </div>
            </div>

            <div className="space-y-2">
              <p className="text-xs text-zinc-300 leading-relaxed">
                The base package for this title is not installed. Deleting will free{' '}
                <strong className="text-rose-400 font-mono font-bold">
                  {formatBytes(selectedLeftoverToDelete.total_size)}
                </strong>{' '}
                from console internal storage.
              </p>
              <p className="text-xs font-semibold text-zinc-400">
                The following files and directories will be permanently deleted:
              </p>
            </div>

            {/* Monospace exact paths list */}
            <div className="bg-black/60 border border-white/10 rounded-[2px] p-3 max-h-48 overflow-y-auto space-y-1.5 font-mono text-[11px] text-zinc-300 select-all">
              {selectedLeftoverToDelete.paths && selectedLeftoverToDelete.paths.length > 0 ? (
                selectedLeftoverToDelete.paths.map((p, idx) => (
                  <div key={idx} className="flex items-start space-x-2 text-rose-300/90 break-all">
                    <span className="text-rose-500/70 select-none">✕</span>
                    <span>{p}</span>
                  </div>
                ))
              ) : (
                <div className="text-zinc-500 italic">No paths recorded.</div>
              )}
            </div>

            <p className="text-[11px] text-zinc-400">
              This action cannot be undone.
            </p>

            <div className="flex items-center justify-end space-x-3 pt-2">
              <button
                type="button"
                onClick={() => setSelectedLeftoverToDelete(null)}
                disabled={deletingLeftover}
                className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-300 text-xs font-semibold transition-colors cursor-pointer disabled:opacity-50"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={() => handleConfirmDeleteLeftover(selectedLeftoverToDelete)}
                disabled={deletingLeftover}
                className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-xs font-bold transition-colors cursor-pointer disabled:opacity-50 flex items-center space-x-2"
              >
                {deletingLeftover ? (
                  <>
                    <div className="ps5-robust-spinner-sm" />
                    <span>Deleting Leftovers...</span>
                  </>
                ) : (
                  <>
                    <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                      <polyline points="3 6 5 6 21 6" />
                      <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                    </svg>
                    <span>Delete Permanently ({formatBytes(selectedLeftoverToDelete.total_size)})</span>
                  </>
                )}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

