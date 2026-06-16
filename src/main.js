const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

// ── DOM refs ──────────────────────────────────────────────────────────────────
let resultEl, statusEl, wordPopup, wordPopupText, wordPopupClose;

// ── Status helper ─────────────────────────────────────────────────────────────
function setStatus(msg, type = "") {
  statusEl.textContent = msg;
  statusEl.className = "status" + (type ? " " + type : "");
}

// ── Word popup ────────────────────────────────────────────────────────────────
function showWordPopup(word) {
  if (!word) {
    setStatus("No word found under cursor", "warn");
    return;
  }
  wordPopupText.textContent = word;
  wordPopup.classList.add("visible");
  // Auto-dismiss after 6 s
  clearTimeout(wordPopup._timer);
  wordPopup._timer = setTimeout(() => hideWordPopup(), 6000);
}

function hideWordPopup() {
  wordPopup.classList.remove("visible");
}

// ── OCR helpers ───────────────────────────────────────────────────────────────
async function ocrFile(filePath) {
  return await invoke("ocr_from_file", { path: filePath });
}
async function ocrScreen() {
  return await invoke("ocr_screenshot");
}
async function ocrWordAtCursor() {
  return await invoke("ocr_word_at_cursor");
}

// ── Init ──────────────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", async () => {
  resultEl       = document.querySelector("#ocr-result");
  statusEl       = document.querySelector("#ocr-status");
  wordPopup      = document.querySelector("#word-popup");
  wordPopupText  = document.querySelector("#word-popup-text");
  wordPopupClose = document.querySelector("#word-popup-close");

  // ── Listen for hotkey events from Rust ──────────────────────────────────────
  await listen("word-detected", (event) => {
    const word = event.payload;
    showWordPopup(word);
    setStatus(word ? `Detected: "${word}"` : "No word under cursor", word ? "" : "warn");
  });

  await listen("word-error", (event) => {
    setStatus("Hotkey error: " + event.payload, "error");
  });

  // ── Screenshot button ────────────────────────────────────────────────────────
  document.querySelector("#btn-screenshot").addEventListener("click", async () => {
    setStatus("Scanning screen…");
    resultEl.textContent = "";
    try {
      const text = await ocrScreen();
      resultEl.textContent = text || "(no text detected)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
  });

  // ── File button ──────────────────────────────────────────────────────────────
  const fileInput = document.querySelector("#file-input");
  document.querySelector("#btn-file").addEventListener("click", () => fileInput.click());

  fileInput.addEventListener("change", async () => {
    const file = fileInput.files[0];
    if (!file) return;
    setStatus(`Reading ${file.name}…`);
    resultEl.textContent = "";
    try {
      const text = await ocrFile(file.path);
      resultEl.textContent = text || "(no text detected)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
    fileInput.value = "";
  });

  // ── Word-at-cursor button (manual trigger) ───────────────────────────────────
  document.querySelector("#btn-word").addEventListener("click", async () => {
    setStatus("Detecting word under cursor…");
    try {
      const word = await ocrWordAtCursor();
      showWordPopup(word);
      setStatus(word ? `Detected: "${word}"` : "No word under cursor", word ? "" : "warn");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
  });

  // ── Copy button ──────────────────────────────────────────────────────────────
  document.querySelector("#btn-copy").addEventListener("click", () => {
    const text = resultEl.textContent;
    if (!text) return;
    navigator.clipboard.writeText(text).then(() => setStatus("Copied ✓"));
  });

  // ── Popup close ──────────────────────────────────────────────────────────────
  wordPopupClose.addEventListener("click", hideWordPopup);

  // ── Copy word from popup ─────────────────────────────────────────────────────
  document.querySelector("#word-popup-copy").addEventListener("click", () => {
    const word = wordPopupText.textContent;
    if (word) navigator.clipboard.writeText(word).then(() => setStatus(`"${word}" copied ✓`));
  });
});
