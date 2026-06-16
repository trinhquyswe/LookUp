const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;

const win        = getCurrentWindow();
const wordEl     = document.getElementById('word-text');
const noWordEl   = document.getElementById('no-word');
const progressEl = document.getElementById('progress-bar');
const copyBtn    = document.getElementById('btn-copy');
const closeBtn   = document.getElementById('btn-close');

let autoHideTimer = null;

// ── Auto-hide countdown ───────────────────────────────────────────────────────
function startCountdown() {
  // Restart the CSS animation
  progressEl.style.animation = 'none';
  // Force reflow to restart animation
  void progressEl.offsetWidth;
  progressEl.style.animation = 'drain 5s linear forwards';

  clearTimeout(autoHideTimer);
  autoHideTimer = setTimeout(() => win.hide(), 5000);
}

// ── Show word ─────────────────────────────────────────────────────────────────
function showWord(word) {
  if (word && word.trim()) {
    wordEl.textContent = word.trim();
    wordEl.classList.remove('hidden');
    noWordEl.classList.add('hidden');
    copyBtn.disabled = false;
  } else {
    wordEl.classList.add('hidden');
    noWordEl.classList.remove('hidden');
    copyBtn.disabled = true;
  }
  startCountdown();
}

// ── Listen for word from Rust ─────────────────────────────────────────────────
await listen('show-word', (event) => {
  showWord(event.payload ?? '');
});

// ── Copy to clipboard ─────────────────────────────────────────────────────────
copyBtn.addEventListener('click', async () => {
  const text = wordEl.textContent;
  if (!text) return;
  await navigator.clipboard.writeText(text);
  copyBtn.textContent = 'Copied ✓';
  copyBtn.style.background = 'linear-gradient(135deg, #34d399, #059669)';
  setTimeout(() => {
    copyBtn.innerHTML = `<svg viewBox="0 0 16 16" fill="none">
      <rect x="6" y="6" width="9" height="9" rx="1.5" stroke="currentColor" stroke-width="1.5"/>
      <path d="M3 10H2a1 1 0 01-1-1V2a1 1 0 011-1h7a1 1 0 011 1v1" stroke="currentColor" stroke-width="1.5"/>
    </svg> Copy`;
    copyBtn.style.background = '';
  }, 1500);
});

// ── Dismiss button ────────────────────────────────────────────────────────────
closeBtn.addEventListener('click', () => {
  clearTimeout(autoHideTimer);
  win.hide();
});

// ── Blur = hide (clicking elsewhere dismisses popup) ─────────────────────────
window.addEventListener('blur', () => {
  clearTimeout(autoHideTimer);
  win.hide();
});
