/** @type {import('tailwindcss').Config} */
module.exports = {
    content: ['./chat/**/*.{html,js}'],
    darkMode: 'class',
    theme: {
        extend: {
            colors: {
                primary: '#2b4bee',
                'bg-dark': '#101322',
                'sidebar-dark': '#161b30',
                'bubble-agent': '#1f253e',
                'bubble-user': '#2b4bee',
                'header-dark': '#141829',
                'border-dark': '#1e2340',
                'input-dark': '#181c30',
            },
            fontFamily: {
                sans: ['Inter', '"Noto Sans SC"', 'system-ui', '-apple-system', 'sans-serif'],
                mono: ['"SF Mono"', 'Monaco', 'Menlo', 'monospace'],
            },
        },
    },
    plugins: [],
};
