import { useState } from 'react';
import { scanLeftovers as apiScanLeftovers, deleteLeftover } from '../api/leftovers';
import { formatBytes } from '../utils/formatters';

export function useLeftovers(props) {
  const showToast = props.showToast;
  const fetchStorage = props.fetchStorage;
  const fetchPackagesForDrive = props.fetchPackagesForDrive;
  const selectedDriveRef = props.selectedDriveRef;

  const [leftoversData, setLeftoversData] = useState(null);
  const [scanningLeftovers, setScanningLeftovers] = useState(false);
  const [selectedLeftoverToDelete, setSelectedLeftoverToDelete] = useState(null);
  const [deletingLeftover, setDeletingLeftover] = useState(false);

  const handleScanLeftovers = async () => {
    setScanningLeftovers(true);
    try {
      const data = await apiScanLeftovers();
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
      const data = await deleteLeftover(item.title_id);
      if (data.success) {
        showToast(`Deleted leftovers for ${item.title_name || item.title_id} (freed ${formatBytes(data.freed_bytes || 0)})`, 'success');
        setSelectedLeftoverToDelete(null);
        // Refresh leftovers list
        await handleScanLeftovers();
        // Refresh storage info
        if (fetchStorage) fetchStorage();
        // Trigger rescan of drive packages if on package list
        if (selectedDriveRef && selectedDriveRef.current && fetchPackagesForDrive) {
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
      const data = await apiScanLeftovers();
      const match = Array.isArray(data.leftovers)
        ? data.leftovers.find((l) => l.title_id && l.title_id.toUpperCase() === titleId.toUpperCase())
        : null;
      if (match) {
        setSelectedLeftoverToDelete(match);
        return;
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

  return {
    leftoversData,
    scanningLeftovers,
    selectedLeftoverToDelete,
    setSelectedLeftoverToDelete,
    deletingLeftover,
    handleScanLeftovers,
    handleConfirmDeleteLeftover,
    handleOpenLeftoverCleanupForTitle
  };
}
