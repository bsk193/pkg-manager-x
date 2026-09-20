import { useEffect, useRef } from 'react';

/**
 * Hook to deactivate all interactive elements in the background when any modal is open,
 * preventing PS5 controller spatial navigation from snapping to buttons underneath.
 *
 * @param {boolean} isModalOpen - True if any modal dialog is currently open
 */
export function useModalInert(isModalOpen) {
  const previousFocusRef = useRef(null);

  useEffect(() => {
    if (!isModalOpen) return;

    // 1. Remember previously focused element to restore when modal closes
    if (document.activeElement && document.activeElement !== document.body) {
      previousFocusRef.current = document.activeElement;
    }

    const background = document.getElementById('app-main-content');

    const deactivateElement = (el) => {
      if (!el || el.hasAttribute('data-modal-prev-disabled')) return;

      const wasDisabled = el.disabled === true || el.hasAttribute('disabled');
      el.setAttribute('data-modal-prev-disabled', wasDisabled ? 'true' : 'false');

      const prevTabIndex = el.getAttribute('tabindex');
      el.setAttribute('data-modal-prev-tabindex', prevTabIndex !== null ? prevTabIndex : '__none__');

      if ('disabled' in el) {
        el.disabled = true;
      }
      el.setAttribute('tabindex', '-1');
    };

    const deactivateBackground = () => {
      if (!background) return;

      try {
        background.setAttribute('inert', '');
        background.setAttribute('aria-hidden', 'true');
      } catch (e) {}

      const focusableSelector = 'button, a[href], input, select, textarea, [tabindex]';
      const elements = background.querySelectorAll(focusableSelector);
      elements.forEach(deactivateElement);
    };

    deactivateBackground();

    // 2. Watch for background re-renders while modal is open
    let observer = null;
    if (background && window.MutationObserver) {
      observer = new MutationObserver(() => {
        deactivateBackground();
      });
      observer.observe(background, { childList: true, subtree: true });
    }

    // 3. Move controller focus to the primary/cancel button inside the modal
    const focusTimer = setTimeout(() => {
      const modalDialog = document.querySelector('[data-modal-dialog="true"]') ||
                          document.querySelector('[role="dialog"]');
      if (modalDialog) {
        const focusables = Array.from(modalDialog.querySelectorAll(
          'button:not([disabled]):not([tabindex="-1"]), input:not([disabled]):not([tabindex="-1"]), select:not([disabled]):not([tabindex="-1"]), textarea:not([disabled]):not([tabindex="-1"]), [tabindex]:not([tabindex="-1"])'
        ));

        let target = focusables.find((el) =>
          /cancel|close|maybe later|don't show/i.test(el.textContent || el.getAttribute('aria-label') || '')
        ) || focusables[0];

        if (target && typeof target.focus === 'function') {
          target.focus();
        }
      }
    }, 40);

    // 4. Trap Tab navigation within the modal
    const handleKeyDown = (e) => {
      if (e.key === 'Tab') {
        const modalDialog = document.querySelector('[data-modal-dialog="true"]') ||
                            document.querySelector('[role="dialog"]');
        if (!modalDialog) return;

        const focusables = Array.from(modalDialog.querySelectorAll(
          'button:not([disabled]):not([tabindex="-1"]), input:not([disabled]):not([tabindex="-1"]), select:not([disabled]):not([tabindex="-1"]), textarea:not([disabled]):not([tabindex="-1"]), [tabindex]:not([tabindex="-1"])'
        ));

        if (focusables.length === 0) return;
        const first = focusables[0];
        const last = focusables[focusables.length - 1];

        if (e.shiftKey) {
          if (document.activeElement === first || !modalDialog.contains(document.activeElement)) {
            e.preventDefault();
            last.focus();
          }
        } else {
          if (document.activeElement === last || !modalDialog.contains(document.activeElement)) {
            e.preventDefault();
            first.focus();
          }
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown, true);

    // 5. Cleanup on modal close: restore background states & focus
    return () => {
      clearTimeout(focusTimer);
      window.removeEventListener('keydown', handleKeyDown, true);

      if (observer) {
        observer.disconnect();
      }

      if (background) {
        try {
          background.removeAttribute('inert');
          background.removeAttribute('aria-hidden');
        } catch (e) {}

        const modified = background.querySelectorAll('[data-modal-prev-disabled]');
        modified.forEach((el) => {
          const wasDisabled = el.getAttribute('data-modal-prev-disabled') === 'true';
          if ('disabled' in el) {
            el.disabled = wasDisabled;
          }
          if (wasDisabled) {
            el.setAttribute('disabled', '');
          } else {
            el.removeAttribute('disabled');
          }

          const prevTab = el.getAttribute('data-modal-prev-tabindex');
          if (prevTab && prevTab !== '__none__') {
            el.setAttribute('tabindex', prevTab);
          } else {
            el.removeAttribute('tabindex');
          }

          el.removeAttribute('data-modal-prev-disabled');
          el.removeAttribute('data-modal-prev-tabindex');
        });
      }

      // Restore focus to previous element if it is still in the document
      if (previousFocusRef.current && document.contains(previousFocusRef.current)) {
        try {
          previousFocusRef.current.focus();
        } catch (e) {}
      }
    };
  }, [isModalOpen]);
}
