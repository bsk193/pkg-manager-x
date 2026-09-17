/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'ps-blue': '#0070d1',
        'ps-blue-light': '#0095ff',
        'ps-blue-glow': 'rgba(0, 149, 255, 0.4)',
        'ps-black': '#0a0a0f',
        'ps-surface': '#12131a',
        'ps-card': '#1a1b24',
        'ps-border': 'rgba(255, 255, 255, 0.1)',
        'ps-border-hover': 'rgba(255, 255, 255, 0.3)',
      },
      fontFamily: {
        ps5: ["Inter", "-apple-system", "BlinkMacSystemFont", "Segoe UI", "Roboto", "sans-serif"],
      },
      borderRadius: {
        DEFAULT: '2px',
        'none': '0px',
        'sm': '2px',
        'md': '2px',
        'lg': '2px',
        'xl': '2px',
        '2xl': '2px',
        '3xl': '2px',
        'full': '9999px',
      },
    },
  },
  plugins: [],
}
