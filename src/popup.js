const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;
const { invoke } = window.__TAURI__.core;

const win            = getCurrentWindow();
const wordEl         = document.getElementById('word-text');
const pronunciationEl = document.getElementById('pronunciation');
const audioBtn       = document.getElementById('btn-audio');
const copyBtn        = document.getElementById('btn-copy');
const closeBtn       = document.getElementById('btn-close');
const noWordEl       = document.getElementById('no-word');
const progressEl     = document.getElementById('progress-bar');
const cardEl         = document.getElementById('card');
const tabsBarEl      = document.getElementById('tabs-bar');
const tabViBtn       = document.getElementById('tab-vi');
const tabEnBtn       = document.getElementById('tab-en');
const loadingEl      = document.getElementById('dict-loading');
const listEl         = document.getElementById('definitions-list');

let autoHideTimer = null;
let currentData = null; // Stores lookup result
let audioObj = null;    // Audio element for pronunciation

// ── Auto-hide countdown ───────────────────────────────────────────────────────
function startCountdown() {
  progressEl.style.animation = 'none';
  void progressEl.offsetWidth; // force reflow
  progressEl.style.animation = 'drain 5s linear forwards';
  progressEl.style.animationPlayState = 'running';

  clearTimeout(autoHideTimer);
  autoHideTimer = setTimeout(() => win.hide(), 5000);
}

function pauseCountdown() {
  clearTimeout(autoHideTimer);
  progressEl.style.animationPlayState = 'paused';
}

cardEl.addEventListener('mouseenter', pauseCountdown);
cardEl.addEventListener('mouseleave', startCountdown);

// ── Audio playback ────────────────────────────────────────────────────────────
audioBtn.addEventListener('click', () => {
  if (audioObj) {
    audioObj.currentTime = 0;
    audioObj.play().catch(e => console.error("Audio error:", e));
  }
});

// ── Rendering ────────────────────────────────────────────────────────────────
function renderMeanings(meanings) {
  listEl.innerHTML = '';
  if (!meanings || meanings.length === 0) {
    listEl.innerHTML = '<div class="no-word" style="margin-top: 10px;">Không có định nghĩa nào được tìm thấy.</div>';
    return;
  }

  meanings.forEach(m => {
    const item = document.createElement('div');
    item.className = 'definition-item';

    const pos = document.createElement('span');
    pos.className = 'def-pos';
    pos.textContent = m.pos;

    const def = document.createElement('p');
    def.className = 'def-text';
    def.textContent = m.definition;

    item.appendChild(pos);
    item.appendChild(def);

    if (m.example) {
      const ex = document.createElement('p');
      ex.className = 'def-example';
      ex.textContent = m.example;
      item.appendChild(ex);
    }

    listEl.appendChild(item);
  });
}

function switchTab(lang) {
  if (!currentData) return;
  if (lang === 'vi') {
    tabViBtn.classList.add('active');
    tabEnBtn.classList.remove('active');
    renderMeanings(currentData.vi_meanings);
  } else {
    tabViBtn.classList.remove('active');
    tabEnBtn.classList.add('active');
    renderMeanings(currentData.en_meanings);
  }
}

tabViBtn.addEventListener('click', () => switchTab('vi'));
tabEnBtn.addEventListener('click', () => switchTab('en'));

// ── Show word ─────────────────────────────────────────────────────────────────
async function showWord(word) {
  // Reset UI states
  wordEl.textContent = '';
  pronunciationEl.textContent = '';
  audioBtn.classList.add('hidden');
  tabsBarEl.classList.add('hidden');
  listEl.innerHTML = '';
  loadingEl.classList.add('hidden');
  noWordEl.classList.add('hidden');
  currentData = null;
  audioObj = null;

  const trimmed = word ? word.trim() : '';
  if (!trimmed) {
    noWordEl.classList.remove('hidden');
    copyBtn.disabled = true;
    startCountdown();
    return;
  }

  wordEl.textContent = trimmed;
  copyBtn.disabled = false;
  
  // Show loading skeleton for dictionary lookup
  loadingEl.classList.remove('hidden');
  startCountdown();

  try {
    const res = await invoke('lookup_word', { word: trimmed });
    currentData = res;
    loadingEl.classList.add('hidden');

    // Display pronunciation if available
    if (res.pronunciation) {
      pronunciationEl.textContent = res.pronunciation;
    }

    // Set up audio if available
    if (res.audio) {
      audioObj = new Audio(res.audio);
      audioBtn.classList.remove('hidden');
    }

    const hasVi = res.vi_meanings && res.vi_meanings.length > 0;
    const hasEn = res.en_meanings && res.en_meanings.length > 0;

    if (hasVi && hasEn) {
      tabsBarEl.classList.remove('hidden');
      switchTab('vi');
    } else if (hasVi) {
      switchTab('vi');
    } else if (hasEn) {
      switchTab('en');
    } else {
      listEl.innerHTML = '<div class="no-word" style="margin-top: 10px;">Không tìm thấy nghĩa trong từ điển.</div>';
    }
  } catch (err) {
    console.error(err);
    loadingEl.classList.add('hidden');
    listEl.innerHTML = `<div class="no-word" style="margin-top: 10px; color: #f87171;">Lỗi kết nối từ điển.</div>`;
  }
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
  
  // Visual feedback: briefly change SVG/style
  const origHTML = copyBtn.innerHTML;
  copyBtn.innerHTML = `<svg viewBox="0 0 16 16" fill="none" style="color: #34d399">
    <path d="M5 8.5l2 2 4-4" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
  </svg>`;
  setTimeout(() => {
    copyBtn.innerHTML = origHTML;
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
