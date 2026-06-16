const { invoke } = window.__TAURI__.core;

// ── OCR from a local image file ───────────────────────────────────────────────
async function ocrFile(filePath) {
  return await invoke("ocr_from_file", { path: filePath });
}

// ── OCR the entire screen ─────────────────────────────────────────────────────
async function ocrScreen() {
  return await invoke("ocr_screenshot");
}

// ── UI wiring ─────────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", () => {
  const resultEl = document.querySelector("#ocr-result");
  const statusEl = document.querySelector("#ocr-status");

  function setStatus(msg, isError = false) {
    statusEl.textContent = msg;
    statusEl.className = isError ? "status error" : "status";
  }

  // Screenshot OCR button
  document.querySelector("#btn-screenshot").addEventListener("click", async () => {
    setStatus("Capturing screen…");
    resultEl.textContent = "";
    try {
      const text = await ocrScreen();
      resultEl.textContent = text || "(no text detected)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, true);
    }
  });

  // File OCR: trigger a file dialog via input[type=file]
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
      setStatus("Error: " + err, true);
    }
    fileInput.value = "";
  });

  // Copy to clipboard
  document.querySelector("#btn-copy").addEventListener("click", () => {
    const text = resultEl.textContent;
    if (!text) return;
    navigator.clipboard.writeText(text).then(() => setStatus("Copied to clipboard ✓"));
  });
});
