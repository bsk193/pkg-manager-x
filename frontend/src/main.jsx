import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App.jsx'
import './index.css'

// PS4 tile web view: lighter rendering (see html.ps4-lite in index.css).
try {
  if (new URLSearchParams(window.location.search).get('app') === 'ps4tile') {
    document.documentElement.classList.add('ps4-lite')
  }
} catch (e) {}

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
)

if (window.applicationCache) {
  window.applicationCache.addEventListener('updateready', () => {
    if (window.applicationCache.status === window.applicationCache.UPDATEREADY) {
      window.applicationCache.swapCache();
      window.location.reload();
    }
  }, false);
}
