const { invoke } = window.__TAURI__.core;

// ── Status helper ─────────────────────────────────────────────────────────────
function setStatus(msg, type = "") {
  const el = document.getElementById("ocr-status");
  el.textContent = msg;
  el.className = "status" + (type ? " " + type : "");
}

// ── Init ──────────────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", () => {
  // ── Scan Screen button ───────────────────────────────────────────────────────
  document.getElementById("btn-screenshot").addEventListener("click", async () => {
    setStatus("Scanning screen…");
    const resultEl = document.getElementById("ocr-result");
    resultEl.textContent = "";
    try {
      const text = await invoke("ocr_screenshot");
      resultEl.textContent = text || "(no text detected)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
  });

  // ── Open Image button ────────────────────────────────────────────────────────
  const fileInput = document.getElementById("file-input");
  document.getElementById("btn-file").addEventListener("click", () => fileInput.click());

  fileInput.addEventListener("change", async () => {
    const file = fileInput.files[0];
    if (!file) return;
    setStatus(`Reading ${file.name}…`);
    const resultEl = document.getElementById("ocr-result");
    resultEl.textContent = "";
    try {
      const text = await invoke("ocr_from_file", { path: file.path });
      resultEl.textContent = text || "(no text detected)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
    fileInput.value = "";
  });

  // ── Copy button ──────────────────────────────────────────────────────────────
  document.getElementById("btn-copy").addEventListener("click", () => {
    const text = document.getElementById("ocr-result").textContent;
    if (!text) return;
    navigator.clipboard.writeText(text).then(() => setStatus("Copied ✓"));
  });
});
