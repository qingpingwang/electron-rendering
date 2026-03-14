function setupDivider(el, { axis, onDrag }) {
    let startPos = 0;
    const cls = axis === 'y' ? 'resizing-h' : 'resizing-v';

    el.addEventListener('mousedown', e => {
        e.preventDefault();
        startPos = axis === 'y' ? e.clientY : e.clientX;
        el.classList.add('active');
        document.body.classList.add(cls);

        const onMove = (e) => {
            const cur = axis === 'y' ? e.clientY : e.clientX;
            const delta = cur - startPos;
            startPos = cur;
            onDrag(delta);
        };

        const onUp = () => {
            el.classList.remove('active');
            document.body.classList.remove(cls);
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
        };

        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
    });
}

module.exports = { setupDivider };
