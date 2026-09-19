import { useState, useEffect, useCallback } from 'react';
import { DONATE_MODAL_STORAGE_KEY, DONATE_MODAL_INTERVAL_MS } from '../constants/config';

export function useDonation(props) {
  const initialStatusLoaded = props.initialStatusLoaded;
  const isInstalling = props.isInstalling;
  const isWaitingForPart = props.isWaitingForPart;

  const [showDonateQr, setShowDonateQr] = useState(false);
  const [showDonateModal, setShowDonateModal] = useState(false);
  const [donateNeverNotice, setDonateNeverNotice] = useState(false);
  const [showModalQr, setShowModalQr] = useState(false);

  useEffect(() => {
    if (!initialStatusLoaded) return;
    if (isInstalling || isWaitingForPart) return;

    try {
      const raw = localStorage.getItem(DONATE_MODAL_STORAGE_KEY);
      if (!raw) {
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
  }, [initialStatusLoaded, isInstalling, isWaitingForPart]);

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

  return {
    showDonateQr,
    setShowDonateQr,
    showDonateModal,
    setShowDonateModal,
    donateNeverNotice,
    setDonateNeverNotice,
    showModalQr,
    setShowModalQr,
    handleCloseDonateModal,
    handleNeverShowDonateModal
  };
}
