import React, { useLayoutEffect, useEffect, useMemo, useRef, useState } from 'react';
import { decodeBlurHash } from './blurhash';

// Content-versioned icon URL: mtime+size change when the PKG changes, so the
// browser cache busts exactly when the art does (backend serves ?v= URLs as
// immutable). Falls back to the unversioned URL when version info is absent.
export function iconUrlFor(path, mtime, fileSize) {
  if (!path) return null;
  let url = `/api/icon?path=${encodeURIComponent(path)}`;
  const m = Number(mtime) || 0;
  const s = Number(fileSize) || 0;
  if (m > 0 || s > 0) url += `&v=${m}_${s}`;
  return url;
}

const blurHashCache = new Map();
const MAX_BLURHASH_CACHE = 300;

export function getCachedBlurHash(hash, w = 32, h = 32) {
  if (!hash) return null;
  const key = `${hash}_${w}_${h}`;
  if (blurHashCache.has(key)) {
    return blurHashCache.get(key);
  }
  try {
    const data = decodeBlurHash(hash, w, h);
    const result = { data, w, h };
    if (blurHashCache.size >= MAX_BLURHASH_CACHE) {
      const firstKey = blurHashCache.keys().next().value;
      blurHashCache.delete(firstKey);
    }
    blurHashCache.set(key, result);
    return result;
  } catch (e) {
    return null;
  }
}

// Blurred placeholder + full icon with fade-in.
// Props: pkg ({ path, blurhash, mtime, file_size }) or explicit
// path/blurhash/mtime/fileSize; alt; imgClassName (positioning classes shared
// by both layers); placeholderClassName (extra classes for the canvas layer);
// priority (when true, uses loading="eager" instead of "lazy" to bypass WebKit
// intersection observer delays for visible above-the-fold cards).
export default function BlurIcon({
  pkg,
  path,
  blurhash,
  mtime,
  fileSize,
  alt,
  imgClassName,
  placeholderClassName,
  loading = 'lazy',
  priority = false
}) {
  const p = pkg || {};
  const srcPath = path || p.path;
  const hash = blurhash || p.blurhash || '';
  const verMtime = mtime !== undefined && mtime !== null ? mtime : p.mtime;
  const verSize = fileSize !== undefined && fileSize !== null ? fileSize : p.file_size;
  const url = iconUrlFor(srcPath, verMtime, verSize);

  // ALWAYS initialize loaded to false so the blurred placeholder canvas is
  // guaranteed visible on frame 0, preventing blank squares on mount.
  const [loaded, setLoaded] = useState(false);
  const [attempt, setAttempt] = useState(0);
  const [failed, setFailed] = useState(false);
  const canvasRef = useRef(null);
  const imgRef = useRef(null);
  const urlRef = useRef(url);
  urlRef.current = url;
  const loadedRef = useRef(false);
  const retryTimer = useRef(null);
  const prevUrlRef = useRef(url);

  // Decodes to 32x32 square resolution to match PS5 icon aspect ratio (1:1),
  // avoiding vertical stretch distortion while keeping decode fast and cached.
  const pixels = useMemo(() => getCachedBlurHash(hash, 32, 32), [hash]);

  const handleLoad = () => {
    if (loadedRef.current) return;
    loadedRef.current = true;
    setLoaded(true);
    if (retryTimer.current) {
      clearTimeout(retryTimer.current);
      retryTimer.current = null;
    }
  };

  const handleError = () => {
    if (loadedRef.current) return;
    if (attempt === 0) {
      // Transient failure: keep the placeholder visible and try once more
      // after the burst has passed instead of going permanently blank.
      scheduleRetry();
    } else {
      if (retryTimer.current) {
        clearTimeout(retryTimer.current);
        retryTimer.current = null;
      }
      // Final failure: report back so it shows up in the server log
      // (/api/log) for diagnosis, then fall back to the placeholder.
      try {
        fetch(
          `/api/icon-error?path=${encodeURIComponent(srcPath || '')}&info=attempt${attempt}`,
          { method: 'GET' }
        ).catch(() => {});
      } catch (err) {
        /* ignore */
      }
      setFailed(true);
    }
  };

  const scheduleRetry = () => {
    if (retryTimer.current) return;
    const captured = urlRef.current;
    retryTimer.current = setTimeout(() => {
      retryTimer.current = null;
      // Only refetch if this same URL is still pending (not loaded,
      // not superseded, component still mounted).
      if (captured && !loadedRef.current && urlRef.current === captured) {
        setAttempt(1);
      }
    }, 1500);
  };

  // Synchronously draw pixels to canvas before the browser paints so the placeholder
  // is painted on the very first frame without any flash of blank.
  useLayoutEffect(() => {
    const c = canvasRef.current;
    if (!c || !pixels) return;
    try {
      c.width = pixels.w;
      c.height = pixels.h;
      const ctx = c.getContext('2d');
      if (ctx) ctx.putImageData(new ImageData(pixels.data, pixels.w, pixels.h), 0, 0);
    } catch (e) {
      /* Canvas unavailable: full image still loads on top. */
    }
  }, [pixels, failed]);

  useEffect(() => {
    if (prevUrlRef.current !== url) {
      prevUrlRef.current = url;
      loadedRef.current = false;
      setLoaded(false);
      setAttempt(0);
      setFailed(false);
      if (retryTimer.current) {
        clearTimeout(retryTimer.current);
        retryTimer.current = null;
      }
    }

    const img = imgRef.current;
    if (!img) return;

    // WebKit on PS5 often does NOT dispatch synthetic onLoad events for cached images
    // when nodes are mounted. If the image has already finished loading, transition to loaded.
    // NOTE: Only check complete when naturalWidth > 0; naturalWidth === 0 simply means
    // it is still loading, NOT that it failed.
    if (img.complete && img.naturalWidth > 0) {
      handleLoad();
      return;
    }

    // Attach native load listener in case React's synthetic onLoad does not trigger
    const onNativeLoad = () => handleLoad();
    img.addEventListener('load', onNativeLoad);

    // Safety check on next frame for fast memory/disk cache resolution
    const rafId = requestAnimationFrame(() => {
      if (img.complete && img.naturalWidth > 0) {
        handleLoad();
      }
    });

    return () => {
      img.removeEventListener('load', onNativeLoad);
      cancelAnimationFrame(rafId);
      if (retryTimer.current) {
        clearTimeout(retryTimer.current);
        retryTimer.current = null;
      }
    };
  }, [url, attempt]);

  if (!url) return null;

  const src = attempt > 0 ? `${url}&retry=1` : url;

  // Permanently failed: keep just the blurred placeholder (or nothing when
  // there is no hash). A later url change resets `failed` and retries.
  if (failed) {
    if (!pixels) return null;
    return (
      <canvas
        ref={canvasRef}
        aria-hidden="true"
        className={`absolute inset-0 w-full h-full pointer-events-none ${placeholderClassName || ''}`}
        style={{ filter: 'blur(12px)', transform: 'scale(1.15)' }}
      />
    );
  }

  return (
    <>
      {pixels ? (
        <canvas
          ref={canvasRef}
          aria-hidden="true"
          className={`absolute inset-0 w-full h-full pointer-events-none transition-opacity duration-300 ${
            loaded ? 'opacity-0' : 'opacity-100'
          } ${placeholderClassName || ''}`}
          style={{ filter: 'blur(12px)', transform: 'scale(1.15)' }}
        />
      ) : null}
      <img
        ref={imgRef}
        src={src}
        alt={alt}
        loading={priority ? 'eager' : loading}
        decoding={priority ? 'sync' : 'async'}
        onLoad={handleLoad}
        onError={handleError}
        className={`${imgClassName || ''} transition-opacity duration-300`.trim()}
        style={loaded || !pixels ? undefined : { opacity: 0 }}
      />
    </>
  );
}
