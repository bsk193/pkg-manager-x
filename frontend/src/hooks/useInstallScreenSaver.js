import { useEffect } from 'react';
import { startInstallScreenSaver } from '../utils/installScreenSaver';

export function useInstallScreenSaver(active) {
  useEffect(() => {
    if (!active) return;
    return startInstallScreenSaver(window, document);
  }, [active]);
}
