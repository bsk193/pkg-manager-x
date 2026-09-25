import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'
import fs from 'fs'
import path from 'path'
import { execSync } from 'child_process'

function getBuildInfo() {
  // PKG Manager X: own version (x-v<version> tags) + upstream "based on" version.
  let upstream = process.env.VITE_APP_UPSTREAM_VERSION || ''
  if (!upstream) {
    try {
      const vh = fs.readFileSync(path.resolve(__dirname, '../include/version.h'), 'utf8')
      const m = vh.match(/#define\s+PKGMGR_VERSION\s+"([^"]+)"/)
      if (m) upstream = m[1]
    } catch (e) {}
  }
  let version = process.env.VITE_APP_VERSION || ''
  if (!version) {
    try {
      version = execSync('git describe --tags --match "x-v*"', { encoding: 'utf8' }).trim().replace(/^x-v/, '')
    } catch (e) {}
    if (!version) version = '0.0.0-dev'
  }
  let commit = process.env.VITE_APP_COMMIT || ''
  if (!commit) {
    try {
      commit = execSync('git rev-parse --short HEAD', { encoding: 'utf8' }).trim()
    } catch (e) {}
    if (!commit) commit = 'unknown'
  }
  const date = process.env.VITE_APP_BUILD_DATE || (() => {
    const now = new Date()
    const pad = (n) => String(n).padStart(2, '0')
    return `${now.getUTCFullYear()}-${pad(now.getUTCMonth() + 1)}-${pad(now.getUTCDate())} ${pad(now.getUTCHours())}:${pad(now.getUTCMinutes())}:${pad(now.getUTCSeconds())} UTC`
  })()
  const title = `PKG Manager X v${version} (${commit}, ${date}) - based on PKG Manager v${upstream} by PLK`
  return { version, upstream, commit, date, title }
}

const buildInfo = getBuildInfo()

function titlePlugin(title) {
  return {
    name: 'html-title-transform',
    transformIndexHtml(html) {
      if (html.includes('[[TITLE_PLACEHOLDER]]')) {
        return html.replace(/\[\[TITLE_PLACEHOLDER\]\]/g, title)
      }
      return html.replace(/<title>.*?<\/title>/, `<title>${title}</title>`)
    }
  }
}

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), viteSingleFile(), titlePlugin(buildInfo.title)],
  define: {
    __APP_VERSION__: JSON.stringify(buildInfo.version),
    __APP_UPSTREAM_VERSION__: JSON.stringify(buildInfo.upstream),
    __APP_COMMIT__: JSON.stringify(buildInfo.commit),
    __APP_BUILD_DATE__: JSON.stringify(buildInfo.date),
  },
  build: {
    target: ['es2015', 'safari12'],
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
      },
      format: {
        comments: false,
      },
    },
    cssCodeSplit: false,
    assetsInlineLimit: 10000000,
  },
  server: {
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8844',
        changeOrigin: true
      },
      '/version': {
        target: 'http://127.0.0.1:8844',
        changeOrigin: true
      },
      '/cache.appcache': {
        target: 'http://127.0.0.1:8844',
        changeOrigin: true
      }
    }
  }
})

