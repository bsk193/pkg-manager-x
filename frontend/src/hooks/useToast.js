import { useState, useRef } from 'react';

export function useToast() {
  const [notification, setNotification] = useState(null);
  const toastTimeoutRef = useRef(null);

  const showToast = function(message, type) {
    if (typeof type === 'undefined') type = 'info';
    if (toastTimeoutRef.current) clearTimeout(toastTimeoutRef.current);
    setNotification({ message: message, type: type });
    toastTimeoutRef.current = setTimeout(function() {
      setNotification(null);
      toastTimeoutRef.current = null;
    }, 4000);
  };

  return { notification, showToast };
}
