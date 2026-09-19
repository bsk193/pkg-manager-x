import React from 'react';


export default function Toast({ notification }) {
  if (!notification) return null;
  return (
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
  );
}
