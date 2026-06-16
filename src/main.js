const { invoke } = window.__TAURI__.core;

// ── Status helper ─────────────────────────────────────────────────────────────
function setStatus(msg, type = "") {
  const el = document.getElementById("ocr-status");
  el.innerHTML = msg;
  el.className = "status" + (type ? " " + type : "");
}

// ── Init ──────────────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", async () => {
  // ── Hotkey recorder configuration ──────────────────────────────────────────
  const hotkeyInput = document.getElementById("hotkey-input");
  const saveBtn = document.getElementById("btn-save-hotkey");
  const cancelBtn = document.getElementById("btn-cancel-hotkey");

  let isRecording = false;
  let originalHotkey = "";

  // Fetch initial hotkey
  try {
    const currentHotkey = await invoke("get_hotkey");
    hotkeyInput.value = currentHotkey;
  } catch (err) {
    console.error("Failed to load hotkey:", err);
  }

  hotkeyInput.addEventListener("click", () => {
    if (!isRecording) {
      isRecording = true;
      originalHotkey = hotkeyInput.value;
      hotkeyInput.value = "Press keys...";
      saveBtn.classList.remove("hidden");
      cancelBtn.classList.remove("hidden");
    }
  });

  hotkeyInput.addEventListener("keydown", (e) => {
    if (!isRecording) return;
    e.preventDefault();
    e.stopPropagation();

    const keys = [];
    if (e.ctrlKey) keys.push("Ctrl");
    if (e.shiftKey) keys.push("Shift");
    if (e.altKey) keys.push("Alt");
    if (e.metaKey) keys.push("Super");

    // Capture primary key
    let key = e.key;
    if (key === "Control" || key === "Shift" || key === "Alt" || key === "Meta") {
      if (keys.length > 0) {
        hotkeyInput.value = keys.join("+");
      } else {
        hotkeyInput.value = "Press keys...";
      }
      return;
    }

    // Map keys to standard strings
    if (key === " ") {
      key = "Space";
    } else if (key.length === 1) {
      key = key.toUpperCase();
    } else {
      // Capitalize first letter of special keys (e.g. ArrowUp, Escape)
      key = key.charAt(0).toUpperCase() + key.slice(1);
    }

    keys.push(key);
    hotkeyInput.value = keys.join("+");
    
    // Auto blur/stop recording when primary key is pressed
    isRecording = false;
    hotkeyInput.blur();
  });

  saveBtn.addEventListener("click", async () => {
    isRecording = false;
    const newHotkey = hotkeyInput.value;
    if (newHotkey === "Press keys..." || !newHotkey) {
      hotkeyInput.value = originalHotkey;
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
      return;
    }

    setStatus("Saving hotkey...");
    try {
      await invoke("set_hotkey", { newHotkey });
      setStatus(`Hotkey updated to <strong>${newHotkey}</strong> ✓`);
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
    } catch (err) {
      setStatus("Error: " + err, "error");
      hotkeyInput.value = originalHotkey;
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
    }
  });

  cancelBtn.addEventListener("click", () => {
    isRecording = false;
    hotkeyInput.value = originalHotkey;
    saveBtn.classList.add("hidden");
    cancelBtn.classList.add("hidden");
  });

  // ── Word at Cursor button ───────────────────────────────────────────────────
  document.getElementById("btn-word").addEventListener("click", async () => {
    setStatus("Scanning word under mouse…");
    const resultEl = document.getElementById("ocr-result");
    resultEl.textContent = "";
    try {
      const text = await invoke("ocr_word_at_cursor");
      resultEl.textContent = text || "(no word found under cursor)";
      setStatus("Done ✓");
    } catch (err) {
      setStatus("Error: " + err, "error");
    }
  });

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
